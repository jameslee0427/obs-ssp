#include "ssp-toolbar.h"
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QApplication>
#include <QCloseEvent>
#include <QToolButton>
#include <QPointer>
#include <browser-panel.hpp>
#include <QTimer>
#include <random>
#include <QThread>

SspToolbarManager* SspToolbarManager::s_instance = nullptr;
QCef* SspToolbarManager::m_qcef = nullptr;
QCefCookieManager* SspToolbarManager::panel_cookies = nullptr;

SspToolbarManager* SspToolbarManager::instance()
{
    if (!s_instance) {
        // Ensure instance is created on the main thread if not already
        if (QApplication::instance() && QThread::currentThread() != QApplication::instance()->thread()) {
            QMetaObject::invokeMethod(QApplication::instance(), []() {
                if (!s_instance) { // Double check after switching to main thread
                    s_instance = new SspToolbarManager();
                }
            }, Qt::BlockingQueuedConnection);
        } else {
            if (!s_instance) { // If already in main thread or no QApplication
                 s_instance = new SspToolbarManager();
            }
        }
    }
    return s_instance;
}

static std::string GenId()
{
	std::random_device rd;
	std::mt19937_64 e2(rd());
	std::uniform_int_distribution<uint64_t> dist(0, 0xFFFFFFFFFFFFFFFF);

	uint64_t id = dist(e2);

	char id_str[20];
	snprintf(id_str, sizeof(id_str), "%16llX", (unsigned long long)id);
	return std::string(id_str);
}

SspToolbarManager::SspToolbarManager(QObject *parent)
	: QObject(parent), m_toolbar(nullptr), m_mainWindow(nullptr), m_browserInitialized(false)
{
	m_mainWindow = (QMainWindow *)obs_frontend_get_main_window();
    
    // Create initialization timer
    m_initTimer = new QTimer(this);
    connect(m_initTimer, &QTimer::timeout, this, &SspToolbarManager::checkBrowserInitialization);

    // Connect signals for thread-safe operations
    connect(this, &SspToolbarManager::addSourceActionRequested,
            this, &SspToolbarManager::doAddSourceAction, Qt::QueuedConnection);
    connect(this, &SspToolbarManager::removeSourceActionRequested,
            this, &SspToolbarManager::doRemoveSourceAction, Qt::QueuedConnection);
    
    // Start browser initialization
    initializeBrowserPanel();
}

SspToolbarManager::~SspToolbarManager()
{
    if (m_initTimer) {
        m_initTimer->stop();
        m_initTimer->deleteLater();
    }
    
    // Toolbar is cleaned up by shutdown or directly if no shutdown called
    // if (m_toolbar) { 
    //     removeToolbar(); // This might be too late if shutdown is used
    // }
    
    // Note: Dock and browser resource cleanup is primarily handled in shutdown()
    // If shutdown() is not called, this destructor will attempt cleanup, 
    // but it might not be as robust for active browser instances.
    m_browserDocks.clear(); // Pointers in m_browserDocks are owned by m_mainWindow

    if (panel_cookies) {
	    panel_cookies->FlushStore();
	    delete panel_cookies;
	    panel_cookies = nullptr;
    }
    if (m_qcef) {
	    delete m_qcef;
	    m_qcef = nullptr;
    }
}

// Public interface method - emits a signal
void SspToolbarManager::addSourceAction(const QString& sourceName, const QString& ip)
{
    emit addSourceActionRequested(sourceName, ip);
}

// Public interface method - emits a signal
void SspToolbarManager::removeSourceAction(const QString& sourceName, const QString& ip)
{
    emit removeSourceActionRequested(sourceName, ip);
}


// Actual implementation in a slot, executed in the manager's thread
void SspToolbarManager::doAddSourceAction(const QString& sourceName, const QString& ip)
{
    if (!m_toolbar) {
        createToolbar();
    }
    
    QString sourceKey = QString("%1_%2").arg(sourceName).arg(ip);
    
    if (!m_sourceActions.contains(sourceKey)) {
        blog(LOG_ERROR, "add source action in tool bar %s", sourceKey.toStdString().c_str());
        QAction* action = new QAction(sourceName, this); // Parented to this, will be deleted with this
        action->setCheckable(true);
        action->setProperty("themeID", "sspToolbarButton");
        
        connect(action, &QAction::triggered, this, [this, action]() {
            for (QToolButton* btn : m_toolbar->findChildren<QToolButton*>()) {
                if (btn->defaultAction() == action) {
                    btn->setProperty("sspActionButton", true);
                    btn->setStyleSheet("QToolButton:checked { background-color: #5865F2; color: white; border-radius: 2px; }");
                    break;
                }
            }
        });
        
        connect(action, &QAction::toggled, this, [this, sourceName, ip, sourceKey](bool checked) {
            bool dockExists = m_browserDocks.contains(sourceKey);
            bool dockVisible = dockExists && m_browserDocks[sourceKey] && m_browserDocks[sourceKey]->isVisible();
            
            if (checked != dockVisible) {
                if (checked) {
                    if (!dockExists || !m_browserDocks[sourceKey]) { // Check if dock is null also
                        showBrowserDock(sourceName, ip, sourceKey);
                    } else if (m_browserDocks[sourceKey]) {
                        QDockWidget* dock = m_browserDocks[sourceKey];
                        dock->setVisible(true);
                        dock->raise();
                        dock->activateWindow();
                    }
                } else if (dockExists && m_browserDocks[sourceKey]) {
                    m_browserDocks[sourceKey]->setVisible(false);
                }
            }
        });
        
        m_sourceActions[sourceKey] = action;
        if (m_toolbar) { // Ensure toolbar exists before adding action
             m_toolbar->addAction(action);
        }
    }
}

// Actual implementation in a slot, executed in the manager's thread
void SspToolbarManager::doRemoveSourceAction(const QString& sourceName, const QString& ip)
{
    QString sourceKey = QString("%1_%2").arg(sourceName).arg(ip);
    blog(LOG_ERROR, "remove source action in tool bar %s", sourceKey.toStdString().c_str());
    if (m_sourceActions.contains(sourceKey)) {
        QAction* action = m_sourceActions[sourceKey];
        if(m_toolbar!=nullptr)
            m_toolbar->removeAction(action);
        // action is parented to this, will be deleted when this is deleted, or if explicitly deleted here:
        delete action; 
        m_sourceActions.remove(sourceKey);
    }

    if (m_browserDocks.contains(sourceKey)) {
        QDockWidget* dock = m_browserDocks.take(sourceKey); // Use take to remove and get value
	    if (dock != nullptr) {
            QCefWidget * w = (QCefWidget*)(dock->widget());
            if (w != nullptr) {
		        w->closeBrowser();
            }
		    if (m_mainWindow != nullptr)
			    m_mainWindow->removeDockWidget(dock);
		    dock->deleteLater(); // Use deleteLater for QObject cleanup
	    }
    }

    if (m_toolbar && m_sourceActions.isEmpty()) { // Check if toolbar exists
        removeToolbar();
    }
}

void SspToolbarManager::createToolbar()
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "createToolbar", Qt::QueuedConnection);
        return;
    }
    if (!m_toolbar) {
        m_toolbar = new QToolBar(obs_module_text("SSPPlugin.Toolbar.Title"), m_mainWindow);
        m_toolbar->setObjectName("sspToolbar");
        m_toolbar->setProperty("themeID", "sspToolbar");
        QString styleSheet = "QToolBar::separator { width: 2px; }";
        styleSheet += "QToolButton:checked { background-color: rgb(88, 101, 242); border-radius: 2px; }";
        m_toolbar->setStyleSheet(styleSheet);
        m_mainWindow->addToolBar(m_toolbar);
    }
}

void SspToolbarManager::removeToolbar()
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "removeToolbar", Qt::QueuedConnection);
        return;
    }
    if (m_toolbar) {
        if (m_mainWindow) { // Check if mainWindow is still valid
            m_mainWindow->removeToolBar(m_toolbar);
        }
	    m_toolbar->deleteLater();
        m_toolbar = nullptr;
    }
    // DO NOT delete s_instance here. Its lifecycle is managed by shutdown().
}

void SspToolbarManager::initializeBrowserPanel() {
    if (!m_qcef) {
        m_qcef = obs_browser_init_panel();
        if (m_qcef) {
            m_qcef->init_browser();
            // Start polling for initialization
            m_initTimer->start(100); // Check every 100ms
        } else {
            blog(LOG_ERROR, "Failed to initialize browser panel");
        }
    }
}

void SspToolbarManager::checkBrowserInitialization() {
    if (m_qcef && m_qcef->initialized()) {
        m_browserInitialized = true;
        m_initTimer->stop();
        
        // Initialize cookies if not already done
        if (!panel_cookies) {
            std::string sub_path;
            sub_path += "imvt/";
            sub_path += GenId();
            panel_cookies = m_qcef->create_cookie_manager(sub_path);
        }
        
        // Process any pending browser dock requests
        processPendingDocks();
        
        blog(LOG_INFO, "Browser panel initialized successfully");
    }
}

void SspToolbarManager::processPendingDocks() {
    // Process any docks that were created before browser was ready
    for (auto it = m_browserDocks.begin(); it != m_browserDocks.end(); ++it) {
        QDockWidget* dock = it.value();
        if (dock && !dock->widget()) {
            QString sourceKey = it.key();
            QString sourceName = dock->windowTitle();
            QString ip = sourceKey.split('_').last();
            createBrowserWidget(dock, sourceName, ip);
        }
    }
}

void SspToolbarManager::suppressUnloadDialog(QCefWidget* widget) {
    if (widget) {
        blog(LOG_INFO, "Attempting to suppress unload dialog via JavaScript.");
        widget->executeJavaScript("window.onbeforeunload = null;");
    }
}

void SspToolbarManager::scheduleSuppressUnloadDialog(const QString& sourceKey) {
    // Use a QTimer::singleShot to delay the execution.
    // The delay (e.g., 1000ms) allows the page to potentially set its own onbeforeunload handler first.
    QTimer::singleShot(1000, this, [this, sourceKey]() {
        if (!m_browserDocks.contains(sourceKey)) {
            blog(LOG_WARNING, "Dock for key %s no longer exists, cannot suppress unload dialog.", sourceKey.toStdString().c_str());
            return;
        }
        QDockWidget* dock = m_browserDocks.value(sourceKey);
        if (dock && dock->widget()) {
            QCefWidget* browser = (QCefWidget*)(dock->widget());
            if (browser) {
                suppressUnloadDialog(browser);
            } else {
                blog(LOG_WARNING, "No QCefWidget found in dock for key %s.", sourceKey.toStdString().c_str());
            }
        } else {
            blog(LOG_WARNING, "Dock or its widget is null for key %s.", sourceKey.toStdString().c_str());
        }
    });
}

QCefWidget* SspToolbarManager::createBrowserWidget(QDockWidget* dock, const QString& sourceName, const QString& ip) {
    if (!m_qcef || !m_qcef->initialized() || !panel_cookies) {
        return nullptr;
    }
    std::string url = "http://" + ip.toStdString();
    QCefWidget* cefWidget = m_qcef->create_widget(dock, url, panel_cookies);
    if (cefWidget) {
        // Potentially set startup script here too for the very first load
        cefWidget->setStartupScript("window.onbeforeunload = null;");
        // It's often better to set it *before* create_widget if the API supports it, 
        // or immediately after creation BEFORE any navigation starts if create_widget takes an initial URL.
        // Since create_widget here takes the URL, the script should be set on the created cefWidget.
        dock->setWidget(cefWidget);
        cefWidget->setVisible(true);
        cefWidget->setFocus();
        // No longer using scheduleSuppressUnloadDialog if startup script is effective
    }
    return cefWidget;
}

QDockWidget* SspToolbarManager::createBrowserDock(const QString& sourceName, const QString& ip, const QString& sourceKey)
{
    QDockWidget* dock = new QDockWidget(sourceName, m_mainWindow);
    dock->setObjectName("sspDock_" + sourceKey); // Set object name to retrieve key later
    
    // Only enable floating and movable features, NOT closable
    dock->setFeatures(QDockWidget::DockWidgetFloatable | 
                      QDockWidget::DockWidgetMovable);
    
    // Add a custom title bar with hide button instead of close button
    QWidget* titleBar = new QWidget(dock);
    QHBoxLayout* layout = new QHBoxLayout(titleBar);
    layout->setContentsMargins(5, 0, 0, 0);
    layout->setSpacing(0);
    
    QLabel* titleLabel = new QLabel(sourceName, titleBar);
    titleLabel->setStyleSheet("font-weight: bold;");
    
    QToolButton* hideButton = new QToolButton(titleBar);
    hideButton->setText("X");
    hideButton->setAutoRaise(true);
    hideButton->setToolTip(tr("Hide"));
    
    connect(hideButton, &QToolButton::clicked, dock, &QDockWidget::hide);
    
    layout->addWidget(titleLabel);
    layout->addStretch();
    layout->addWidget(hideButton);
    
    dock->setTitleBarWidget(titleBar);
    
    // Set initial size and properties
    dock->resize(1280, 720);
    dock->setMinimumSize(600, 480);
    dock->setWindowTitle(sourceName);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setFloating(true);
    dock->setProperty("themeID", "sspDockTheme");
    
    // Only create browser widget if browser is initialized
    if (m_browserInitialized) {
        QCefWidget* browser = createBrowserWidget(dock, sourceName, ip);
        // The scheduling is now handled inside createBrowserWidget if successful
    }
    
    m_mainWindow->addDockWidget(Qt::LeftDockWidgetArea, dock);
    
    // Make sure the dock appears in front and has focus
    dock->show();
    dock->raise();
    dock->activateWindow();
    
    // Set button to checked state initially
    if (m_sourceActions.contains(sourceKey)) {
        if (m_sourceActions[sourceKey]->isChecked() != true) {
            m_sourceActions[sourceKey]->setChecked(true);
        }
    }
    
    return dock;
}

void SspToolbarManager::showBrowserDock(const QString& sourceName, const QString& ip, const QString& sourceKey)
{
    if (!m_browserDocks.contains(sourceKey) || !m_browserDocks.value(sourceKey)) {
        QDockWidget* dock = createBrowserDock(sourceName, ip, sourceKey);
        m_browserDocks[sourceKey] = dock;
        
        connect(dock, &QDockWidget::visibilityChanged, this, [this, sourceName, ip, sourceKey](bool visible) {
            if (m_sourceActions.contains(sourceKey)) {
                QAction* action = m_sourceActions[sourceKey];
                if (action->isChecked() != visible) {
                    action->setChecked(visible);
                }
            }            
            if (visible && m_browserDocks.contains(sourceKey)) {
                QDockWidget* currentDock = m_browserDocks.value(sourceKey);
                if (currentDock && currentDock->widget()) {
                    QCefWidget* browser = static_cast<QCefWidget*>(currentDock->widget());
                    if (browser) {
                        std::string url = "http://" + ip.toStdString();
                        // Try setting startup script before changing URL
                        browser->setStartupScript("window.onbeforeunload = null;"); 
                        browser->setURL(url);
                        // No need for scheduleSuppressUnloadDialog if startup script works
                    }
                } else if (currentDock && !currentDock->widget() && m_browserInitialized) {
                    createBrowserWidget(currentDock, sourceName, ip);
                }
            }
        });        
        dock->installEventFilter(this);
    } else {
        QDockWidget* dock = m_browserDocks.value(sourceKey);
        if (dock) {
            dock->setFloating(true);
            dock->show();
            dock->raise();
            dock->activateWindow();
            
            if (dock->widget()) {
                QCefWidget* browser = static_cast<QCefWidget*>(dock->widget());
                if (browser) {
                    std::string url = "http://" + ip.toStdString();
                    // Try setting startup script before changing URL
                    browser->setStartupScript("window.onbeforeunload = null;"); 
                    browser->setURL(url);
                    // No need for scheduleSuppressUnloadDialog if startup script works
                    browser->setFocus();
                }
            } else if (m_browserInitialized) {
                 createBrowserWidget(dock, sourceName, ip);
            }
        } else {
            m_browserDocks.remove(sourceKey);
            showBrowserDock(sourceName, ip, sourceKey);
        }
    }
}

bool SspToolbarManager::eventFilter(QObject* watched, QEvent* event)
{
    // Handle visibility changes for dock widgets
    if (event->type() == QEvent::Hide) {
        // Find which dock is being hidden
        for (auto it = m_browserDocks.begin(); it != m_browserDocks.end(); ++it) {
            if (watched == it.value()) {
                QString sourceKey = it.key();                
                // Uncheck the corresponding toolbar button if currently checked
                if (m_sourceActions.contains(sourceKey)) {
                    QAction* action = m_sourceActions[sourceKey];
                    if (action->isChecked()) {
                        action->setChecked(false);
                    }
                }
                break;
            }
        }
    } else if (event->type() == QEvent::Show) {
        // Find which dock is being shown
        for (auto it = m_browserDocks.begin(); it != m_browserDocks.end(); ++it) {
            if (watched == it.value()) {
                QString sourceKey = it.key();
                
                // Check the corresponding toolbar button if not already checked
                if (m_sourceActions.contains(sourceKey)) {
                    QAction* action = m_sourceActions[sourceKey];
                    if (!action->isChecked()) {
                        action->setChecked(true);
                    }
                }
                break;
            }
        }
    } else if (event->type() == QEvent::Close) {
        // Find which dock is being closed
        for (auto it = m_browserDocks.begin(); it != m_browserDocks.end(); ++it) {
            if (watched == it.value()) {
                QString sourceKey = it.key();
                
                // Uncheck the corresponding toolbar button
                if (m_sourceActions.contains(sourceKey)) {
                    QAction* action = m_sourceActions[sourceKey];
                    if (action->isChecked()) {
                        action->setChecked(false);
                    }
                }
                
                // Remove the dock from our map
                m_browserDocks.remove(sourceKey);
                break;
            }
        }
    }
    
    // Pass the event to the base class
    return QObject::eventFilter(watched, event);
}

// Shutdown should also ensure it's called from the main thread or marshal the call
void SspToolbarManager::shutdown() {
    blog(LOG_INFO, "[SSPToolbar] static shutdown() called.");
    if (!s_instance) {
       // blog(LOG_INFO, "[SSPToolbar] static shutdown(): s_instance is null, nothing to do.");
        return;
    }

    // Check if s_instance is already scheduled for deletion or if its thread is no longer running
    if (s_instance->thread() == nullptr || !s_instance->thread()->isRunning()) {
        //blog(LOG_WARNING, "[SSPToolbar] static shutdown(): s_instance's thread is null or not running. May not be safe to invoke methods.");
        // Potentially, if s_instance is already marked for deletion, just return.
        // However, if cleanup HAS to happen, this is problematic.
        // For now, we proceed, but this state is an indicator of prior issues or very late shutdown.
    }

    if (QThread::currentThread() != s_instance->thread()) {
       // blog(LOG_INFO, "[SSPToolbar] static shutdown(): Marshalling shutdownInternal to s_instance's thread (%p)...", (void*)s_instance->thread());
        bool success = QMetaObject::invokeMethod(s_instance, "shutdownInternal", Qt::BlockingQueuedConnection);
        //blog(LOG_INFO, "[SSPToolbar] static shutdown(): Returned from invokeMethod for shutdownInternal. Success: %s", success ? "true" : "false");
        // If 'success' is false, shutdownInternal was not invoked, which is a major problem.
        // s_instance might be null now if shutdownInternal succeeded and scheduled deletion.
    } else {
       // blog(LOG_INFO, "[SSPToolbar] static shutdown(): Calling shutdownInternal directly on thread %p.", (void*)QThread::currentThread());
        s_instance->shutdownInternal(); 
        //blog(LOG_INFO, "[SSPToolbar] static shutdown(): Returned from direct call to shutdownInternal.");
    }
    blog(LOG_INFO, "[SSPToolbar] static shutdown() finished. s_instance is now %s.", s_instance ? "NOT NULL" : "NULL");
}

// Private internal shutdown method to be executed on the main thread
void SspToolbarManager::shutdownInternal() {
    blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Starting cleanup on thread %p...", (void*)QThread::currentThread());
    
    if (m_initTimer) {
       // blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Stopping init timer...");
        m_initTimer->stop();
        // m_initTimer->deleteLater(); // Defer this or ensure it's safe
       // blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Init timer stopped.");
    }

    blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Closing browser docks (%d docks)...", m_browserDocks.size());
    for (QDockWidget* dock : qAsConst(m_browserDocks).values()) {
        if (dock) {
           // blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Processing dock '%s'...", dock->objectName().toStdString().c_str());
            QCefWidget* browser = (QCefWidget*)(dock->widget());
            if (browser) {
                //blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Calling closeBrowser() for dock '%s'...", dock->objectName().toStdString().c_str());
                browser->closeBrowser();
               // blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Processing events after closeBrowser() for dock '%s'...", dock->objectName().toStdString().c_str());
                QApplication::processEvents(QEventLoop::ProcessEventsFlag::ExcludeUserInputEvents, 100);
            }
            if(m_mainWindow) {
               // blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Removing dock '%s' from main window...", dock->objectName().toStdString().c_str());
                m_mainWindow->removeDockWidget(dock);
            }
            //blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Scheduling dock '%s' for deletion...", dock->objectName().toStdString().c_str());
            dock->deleteLater();
        }
    }
    m_browserDocks.clear();
    blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Browser docks processed and cleared.");

    blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Cleaning up QActions (%d actions)...", m_sourceActions.size());
    qDeleteAll(m_sourceActions.begin(), m_sourceActions.end());
    m_sourceActions.clear();
   // blog(LOG_INFO, "[SSPToolbar] shutdownInternal: QActions cleaned up.");

    if (m_toolbar) {
       // blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Removing and scheduling toolbar for deletion...");
        if(m_mainWindow) m_mainWindow->removeToolBar(m_toolbar);
        m_toolbar->deleteLater();
        m_toolbar = nullptr;
        //blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Toolbar processed.");
    }

    if (panel_cookies) {
        //blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Flushing and deleting panel_cookies...");
        panel_cookies->FlushStore();
        delete panel_cookies;
        panel_cookies = nullptr;
       // blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Panel_cookies cleaned up.");
    }

    if (m_qcef) {
        //blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Processing events before deleting m_qcef...");
        QApplication::processEvents(QEventLoop::ProcessEventsFlag::ExcludeUserInputEvents, 100);
        //blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Deleting m_qcef (%p)...", (void*)m_qcef);
        delete m_qcef;
        m_qcef = nullptr;
        //blog(LOG_INFO, "[SSPToolbar] shutdownInternal: m_qcef deleted.");
    } else {
        //blog(LOG_INFO, "[SSPToolbar] shutdownInternal: m_qcef was already null.");
    }
    
    // Final cleanup of the SspToolbarManager instance itself
    if (s_instance == this) { 
       //blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Nullifying s_instance global pointer and scheduling self (%p) for deletion via deleteLater().", (void*)this);
       s_instance = nullptr; 
       this->deleteLater(); 
    } else {
        //blog(LOG_WARNING, "[SSPToolbar] shutdownInternal: s_instance global pointer was not pointing to this instance (%p vs %p)! This is unexpected.", (void*)s_instance, (void*)this);
        // If s_instance is already null or points elsewhere, just schedule this for deletion if it wasn't the one responsible for s_instance.
        // However, this scenario implies a logic error in singleton management or shutdown sequence.
        this->deleteLater(); 
    }
    blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Finished cleanup (self scheduled for deletion). s_instance is now %s.", s_instance ? "NOT NULL" : "NULL");
}



