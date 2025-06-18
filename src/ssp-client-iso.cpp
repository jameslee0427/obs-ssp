/*
obs-ssp
 Copyright (C) 2019-2020 Yibai Zhang

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; If not, see <https://www.gnu.org/licenses/>
*/

#include <obs.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <memory> // For std::unique_ptr

#ifdef _WIN32
#include <windows.h>
#endif

#if defined(__APPLE__)
#include <dlfcn.h>
#endif

#include <QUuid>
#include <QFileInfo>
#include <QDir>
#include <pthread.h>

#include "obs-ssp.h"
#include "ssp-client-iso.h"
#include <unistd.h>
#include <QCoreApplication>
#include <iostream>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

static size_t os_process_pipe_read_retry(os_process_pipe *pipe, uint8_t *dst,
					 size_t size)
{
	size_t pos = 0, cur = 0;
	while (pos < size) {
		cur = os_process_pipe_read(pipe, dst + pos, size - pos);
		if (!cur) {
			break;
		}
		pos += cur;
	}
	return pos;
}

static Message *msg_recv(os_process_pipe *pipe)
{
	size_t sz = 0;
	Message *msg = (Message *)bmalloc(sizeof(Message));
	if (!msg) {
		return nullptr;
	}
	sz = os_process_pipe_read_retry(pipe, (uint8_t *)msg, sizeof(Message));
	if (sz != sizeof(Message)) {
		ssp_blog(LOG_WARNING, "pipe protocol header error, recv: %d!",
			 sz);
		bfree(msg);
		return nullptr;
	}
	if (msg->length == 0) {
		return msg;
	}
	Message *msg_all = nullptr;
	msg_all = (Message *)bmalloc(sizeof(Message) + msg->length);
	memcpy(msg_all, msg, sizeof(Message));
	bfree(msg);
	//ssp_blog(LOG_INFO, "receive msg type: %d, size: %d", msg_all->type, msg_all->length);
	sz = os_process_pipe_read_retry(pipe, msg_all->value, msg_all->length);
	if (sz != msg_all->length) {
		ssp_blog(LOG_WARNING, "pipe protocol body error, recv: %d!",
			 sz);
		bfree(msg_all);
		return nullptr;
	}
	return msg_all;
}

static void msg_free(Message *msg)
{
	if (msg) {
		bfree(msg);
	}
}

static void *dump_stderr(os_process_pipe *pipe)
{
	size_t sz;
	char buf[1024];
	while (true) {
		sz = os_process_pipe_read_err(pipe, (uint8_t *)buf,
					      sizeof(buf) - 1);
		if (sz == 0) {
			break;
		}
		buf[sz] = '\0';
		ssp_blog(LOG_INFO, "%s", buf);
	}
	ssp_blog(LOG_INFO, "read thread exited");
	return nullptr;
}

SSPClientIso::SSPClientIso(const std::string &ip, uint32_t bufferSize)
{
	this->ip = ip;
	this->bufferSize = bufferSize;
	this->running = false;
	this->pipe = nullptr;

#if defined(__APPLE__)
	Dl_info info;
	dladdr((const void *)msg_free, &info);
	QFileInfo plugin_path(info.dli_fname);
	ssp_connector_path =
		plugin_path.dir().filePath(QStringLiteral(SSP_CONNECTOR));
#else
	ssp_connector_path = QStringLiteral(SSP_CONNECTOR);
#endif
	connect(this, SIGNAL(Start()), this, SLOT(doStart()));
}

SSPClientIso::~SSPClientIso()
{
	// Ensure we stop the client and clean up resources
	Stop();

	// Disconnect all signals to prevent callbacks after destruction
	disconnect(this, SIGNAL(Start()), this, SLOT(doStart()));

	// Clear all callbacks to prevent dangling references
	bufferFullCallback = nullptr;
	audioDataCallback = nullptr;
	metaCallback = nullptr;
	disconnectedCallback = nullptr;
	connectedCallback = nullptr;
	h264DataCallback = nullptr;
	exceptionCallback = nullptr;
}

using namespace std::placeholders;

void SSPClientIso::doStart()
{
	char cwd[1024];
	getcwd(cwd, sizeof(cwd));
	//blog(LOG_INFO, "Current working directory: %s", cwd);
	//ssp_connector_path = QCoreApplication::applicationDirPath() + "/ssp-connector";
	blog(LOG_INFO, "ssp_connector_path: %s",
	     ssp_connector_path.toStdString().c_str());

	//blog(LOG_INFO, "DYLD_LIBRARY_PATH: %s", getenv("DYLD_LIBRARY_PATH") ? getenv("DYLD_LIBRARY_PATH") : "unset");
	//blog(LOG_INFO, "PATH: %s", getenv("PATH"));

	if (access(ssp_connector_path.toStdString().c_str(), X_OK) == 0) {
		blog(LOG_INFO, "ssp-connector is executable");
	} else {
		blog(LOG_WARNING, "ssp-connector access failed: %s (errno=%d)",
		     strerror(errno), errno);
	}

	std::string lib_path =
		QFileInfo(ssp_connector_path).absolutePath().toStdString() +
		"/../Frameworks/libssp.dylib";
	void *handle = dlopen(lib_path.c_str(), RTLD_LAZY);
	if (handle) {
		blog(LOG_INFO, "dlopen succeeded for libssp.dylib");
		dlclose(handle);
	} else {
		blog(LOG_WARNING, "dlopen failed for libssp.dylib: %s",
		     dlerror());
	}

	qDebug()
		<< "[obs-ssp] Starting ssp-connector at:" << ssp_connector_path;
	blog(LOG_INFO, "[obs-ssp] Starting ssp-connector at: %s",
	     ssp_connector_path.toUtf8().constData());

	// Normalize path
	ssp_connector_path = QDir::toNativeSeparators(ssp_connector_path);

	os_process_args_t *args = os_process_args_create(
		ssp_connector_path.toStdString().c_str());
	os_process_args_add_arg(args, "--host");
	os_process_args_add_arg(args, this->ip.c_str());
	os_process_args_add_arg(args, "--port");
	os_process_args_add_arg(args, "9999");

	auto tpipe = os_process_pipe_create2(args, "r");
	blog(LOG_INFO, "Start ssp-connector with args");

	os_process_args_destroy(args);

	if (!tpipe) {
		blog(LOG_WARNING, "Start ssp-connector failed: %s (errno=%d)",
		     strerror(errno), errno);
		char error_buf[1024] = {0};
		int err = os_process_pipe_read_err(tpipe, (uint8_t *)error_buf,
						   sizeof(error_buf) - 1);
		if (err > 0) {
			blog(LOG_WARNING, "ssp-connector error output: %s",
			     error_buf);
		} else {
			blog(LOG_WARNING,
			     "No error output captured from ssp-connector");
		}
		return;
	}

	this->statusLock.lock();
	this->running = true;
	this->pipe = tpipe;
	this->worker = std::thread(SSPClientIso::ReceiveThread, this);
	this->statusLock.unlock();
}

void *SSPClientIso::ReceiveThread(void *arg)
{
	auto th = (SSPClientIso *)arg;
	Message *msg = nullptr;
	th->statusLock.lock();
	auto pipe = th->pipe;
	th->statusLock.unlock();

#ifdef _WIN32
	std::thread(dump_stderr, pipe).detach();
#endif

	// Use RAII to ensure message is freed
	std::unique_ptr<Message, decltype(&msg_free)> initial_msg(
		msg_recv(pipe), msg_free);
	if (!initial_msg) {
		blog(LOG_WARNING, "%s Receive error !", th->getIp().c_str());
		return nullptr;
	}
	if (initial_msg->type != MessageType::ConnectorOkMsg) {
		blog(LOG_WARNING, "%s Protocol error !", th->getIp().c_str());
		return nullptr;
	}

	while (th->running) {
		// Use RAII to ensure message is freed
		std::unique_ptr<Message, decltype(&msg_free)> msg(
			msg_recv(pipe), msg_free);
		if (!msg) {
			blog(LOG_WARNING, "%s Receive error !",
			     th->getIp().c_str());
			break;
		}

		switch (msg->type) {
		case MessageType::MetaDataMsg:
			th->OnMetadata((Metadata *)msg->value);
			break;
		case MessageType::VideoDataMsg:
			th->OnH264Data((VideoData *)msg->value);
			break;
		case MessageType::AudioDataMsg:
			th->OnAudioData((AudioData *)msg->value);
			break;
		case MessageType::RecvBufferFullMsg:
			th->OnRecvBufferFull();
			break;
		case MessageType::DisconnectMsg:
			th->OnDisconnected();
			break;
		case MessageType::ConnectionConnectedMsg:
			th->OnConnectionConnected();
			break;
		case MessageType::ExceptionMsg:
			th->OnException((Message *)msg->value);
			break;
		default:
			blog(LOG_WARNING, "Protocol error !");
			break;
		}
	}

	blog(LOG_WARNING, "%s Receive thread exit !", th->getIp().c_str());
	return nullptr;
}

void SSPClientIso::Restart()
{
	this->Stop();
	emit this->Start();
}

void SSPClientIso::Stop()
{
	blog(LOG_INFO, "ssp client %s stopping...", ip.c_str());
	if (!running) {
		blog(LOG_INFO, "ssp client %s already stopped...", ip.c_str());
		return;
	}
	this->statusLock.lock();
	this->running = false;
	this->statusLock.unlock();
	if (this->worker.joinable()) {
		this->worker.join();
	}
	if (this->pipe) {
		os_process_pipe_destroy(this->pipe);
		this->pipe = nullptr;
	}
}

void SSPClientIso::OnRecvBufferFull()
{
	this->bufferFullCallback();
}

void SSPClientIso::OnH264Data(VideoData *videoData)
{
	imf::SspH264Data video;
	video.frm_no = videoData->frm_no;
	video.ntp_timestamp = videoData->ntp_timestamp;
	video.pts = videoData->pts;
	video.type = videoData->type;
	video.len = videoData->len;
	video.data = videoData->data;
	this->h264DataCallback(&video);
}
void SSPClientIso::OnAudioData(AudioData *audioData)
{
	struct imf::SspAudioData audio;
	audio.ntp_timestamp = audioData->ntp_timestamp;
	audio.pts = audioData->pts;
	audio.len = audioData->len;
	audio.data = audioData->data;
	this->audioDataCallback(&audio);
}
void SSPClientIso::OnMetadata(Metadata *metadata)
{

	struct imf::SspVideoMeta vmeta;
	struct imf::SspAudioMeta ameta;
	struct imf::SspMeta meta;

	ameta.bitrate = metadata->ameta.bitrate;
	ameta.channel = metadata->ameta.channel;
	ameta.encoder = metadata->ameta.encoder;
	ameta.sample_rate = metadata->ameta.sample_rate;
	ameta.sample_size = metadata->ameta.sample_size;
	ameta.timescale = metadata->ameta.timescale;
	ameta.unit = metadata->ameta.unit;

	vmeta.encoder = metadata->vmeta.encoder;
	vmeta.gop = metadata->vmeta.gop;
	vmeta.height = metadata->vmeta.height;
	vmeta.timescale = metadata->vmeta.timescale;
	vmeta.unit = metadata->vmeta.unit;
	vmeta.width = metadata->vmeta.width;

	meta.pts_is_wall_clock = metadata->meta.pts_is_wall_clock;
	meta.tc_drop_frame = metadata->meta.tc_drop_frame;
	meta.timecode = metadata->meta.timecode;

	this->metaCallback(&vmeta, &ameta, &meta);
}
void SSPClientIso::OnDisconnected()
{
	this->disconnectedCallback();
}
void SSPClientIso::OnConnectionConnected()
{
	this->connectedCallback();
}
void SSPClientIso::OnException(Message *exception)
{
	this->exceptionCallback(exception->type, (char *)exception->value);
}

void SSPClientIso::setOnRecvBufferFullCallback(
	const imf::OnRecvBufferFullCallback &cb)
{
	this->bufferFullCallback = cb;
}

void SSPClientIso::setOnAudioDataCallback(const imf::OnAudioDataCallback &cb)
{
	this->audioDataCallback = cb;
}

void SSPClientIso::setOnMetaCallback(const imf::OnMetaCallback &cb)
{
	this->metaCallback = cb;
}

void SSPClientIso::setOnDisconnectedCallback(
	const imf::OnDisconnectedCallback &cb)
{
	this->disconnectedCallback = cb;
}

void SSPClientIso::setOnConnectionConnectedCallback(
	const imf::OnConnectionConnectedCallback &cb)
{
	this->connectedCallback = cb;
}

void SSPClientIso::setOnH264DataCallback(const imf::OnH264DataCallback &cb)
{
	this->h264DataCallback = cb;
}

void SSPClientIso::setOnExceptionCallback(const imf::OnExceptionCallback &cb)
{
	this->exceptionCallback = cb;
}
