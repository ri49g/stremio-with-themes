// main.cpp
#include <QQmlApplicationEngine>
#include <QtWebEngine>
#include <QSysInfo>

#include <clocale>

#define APP_TITLE "Stremio - Freedom to Stream"

#define DESKTOP true

#ifdef DESKTOP
#include <QtWidgets/QApplication>
typedef QApplication Application;

#include <QQmlEngine>
#include <QQmlContext>

#include <QStandardPaths>
#include <QSystemTrayIcon>

#include "systemtray.h"
#include "mainapplication.h"
#include "stremioprocess.h"
#include "mpv.h"
#include "screensaver.h"
#include "razerchroma.h"
#include "qclipboardproxy.h"

#include <QObject>
#include <QFile>
#include <QDebug>
#include <QDialog>
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

#else
#include <QGuiApplication>
#endif

class CssLoader : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString cssContent READ cssContent NOTIFY cssContentChanged)

public:
    CssLoader(QObject *parent = nullptr) : QObject(parent) {
        // Read the CSS file
        QFile file("/home/ras/ephemeral/mods/theme.css");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_cssContent = file.readAll();
            file.close();
        } else {
            m_cssContent = "";
            qWarning() << "Could not open CSS file";
        }
    }

    QString cssContent() const {
        return m_cssContent;
    }

signals:
    void cssContentChanged();

private:
    QString m_cssContent;
};

class JsLoader : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString jsContent READ jsContent NOTIFY jsContentChanged)

public:
    JsLoader(QObject *parent = nullptr) : QObject(parent) {
        // Read the JS file
        QFile file("/home/ras/ephemeral/mods/mod.js");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_jsContent = file.readAll();
            file.close();
        } else {
            m_jsContent = "";
            qWarning() << "Could not open JS file";
        }
    }

    QString jsContent() const {
        return m_jsContent;
    }

signals:
    void jsContentChanged();

private:
    QString m_jsContent;
};

// A small dialog to control the mod and theming settings
class ModSettingsDialog : public QDialog {
    Q_OBJECT
    Q_PROPERTY(bool themingEnabled READ themingEnabled WRITE setThemingEnabled NOTIFY themingEnabledChanged)
    Q_PROPERTY(bool modEnabled READ modEnabled WRITE setModEnabled NOTIFY modEnabledChanged)

public:
    ModSettingsDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("Mod & Theme Settings");
        setModal(false);

        // Create UI elements
        m_themingCheck = new QCheckBox("Enable Theming");
        m_modCheck = new QCheckBox("Enable Mod");
        m_reloadButton = new QPushButton("Reload");

        m_themingCheck->setChecked(true);
        m_modCheck->setChecked(true);

        QVBoxLayout *vLayout = new QVBoxLayout(this);
        vLayout->addWidget(m_themingCheck);
        vLayout->addWidget(m_modCheck);

        QHBoxLayout *hLayout = new QHBoxLayout();
        hLayout->addStretch(1);
        hLayout->addWidget(m_reloadButton);
        vLayout->addLayout(hLayout);

        connect(m_themingCheck, &QCheckBox::toggled, this, &ModSettingsDialog::setThemingEnabled);
        connect(m_modCheck, &QCheckBox::toggled, this, &ModSettingsDialog::setModEnabled);
        connect(m_reloadButton, &QPushButton::clicked, this, &ModSettingsDialog::onReloadClicked);

        resize(200, 120);
    }

    bool themingEnabled() const { return m_themingEnabled; }
    bool modEnabled() const { return m_modEnabled; }

public slots:
    void setThemingEnabled(bool enabled) {
        if (m_themingEnabled != enabled) {
            m_themingEnabled = enabled;
            emit themingEnabledChanged();
        }
    }

    void setModEnabled(bool enabled) {
        if (m_modEnabled != enabled) {
            m_modEnabled = enabled;
            emit modEnabledChanged();
        }
    }

    Q_INVOKABLE void showDialog() {
        show();
        raise();
        activateWindow();
    }

    Q_INVOKABLE void toggleDialog() {
        if (isVisible()) {
            hide();
        } else {
            showDialog();
        }
    }

signals:
    void themingEnabledChanged();
    void modEnabledChanged();
    void reloadRequested();

private slots:
    void onReloadClicked() {
        emit reloadRequested();
    }

private:
    bool m_themingEnabled = true;
    bool m_modEnabled = true;
    QCheckBox *m_themingCheck;
    QCheckBox *m_modCheck;
    QPushButton *m_reloadButton;
};

void InitializeParameters(QQmlApplicationEngine *engine, MainApp& app, ModSettingsDialog *modSettings) {
    QQmlContext *ctx = engine->rootContext();
    SystemTray * systemTray = new SystemTray();

    ctx->setContextProperty("applicationDirPath", QGuiApplication::applicationDirPath());
    ctx->setContextProperty("appTitle", QString(APP_TITLE));
    ctx->setContextProperty("autoUpdater", app.autoupdater);

    // Set access to an object of class properties in QML context
    ctx->setContextProperty("systemTray", systemTray);

    #ifdef QT_DEBUG
        ctx->setContextProperty("debug", true);
    #else
        ctx->setContextProperty("debug", false);
    #endif

    // Add the CssLoader instance
    CssLoader *cssLoader = new CssLoader();
    ctx->setContextProperty("cssLoader", cssLoader);

    // Add the JsLoader instance
    JsLoader *jsLoader = new JsLoader();
    ctx->setContextProperty("jsLoader", jsLoader);

    // Add the mod settings dialog instance
    ctx->setContextProperty("modSettingsDialog", modSettings);
}


int main(int argc, char **argv)
{
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--autoplay-policy=no-user-gesture-required");
    #ifdef _WIN32
    Application::setAttribute(Qt::AA_UseOpenGLES);
    auto winVer = QSysInfo::windowsVersion();
    if(winVer <= QSysInfo::WV_WINDOWS8 && winVer != QSysInfo::WV_None) {
        qputenv("NODE_SKIP_PLATFORM_CHECK", "1");
    }
    if(winVer <= QSysInfo::WV_WINDOWS7 && winVer != QSysInfo::WV_None) {
        qputenv("QT_ANGLE_PLATFORM", "d3d9");
    }
    #endif

    #ifndef Q_OS_LINUX
    Application::setAttribute(Qt::AA_EnableHighDpiScaling);
    #endif

    Application::setApplicationName("Stremio");
    Application::setApplicationVersion(STREMIO_SHELL_VERSION);
    Application::setOrganizationName("Smart Code ltd");
    Application::setOrganizationDomain("stremio.com");

    MainApp app(argc, argv, true);
    #ifndef Q_OS_MACOS
    if( app.isSecondary() ) {
        if( app.arguments().count() > 1)
            app.sendMessage( app.arguments().at(1).toUtf8() );
        else
            app.sendMessage( "SHOW" );
        return 0;
    }
    #endif

    app.setWindowIcon(QIcon(":/images/stremio_window.png"));

    std::setlocale(LC_NUMERIC, "C");

    static QQmlApplicationEngine* engine = new QQmlApplicationEngine();

    qmlRegisterType<Process>("com.stremio.process", 1, 0, "Process");
    qmlRegisterType<ScreenSaver>("com.stremio.screensaver", 1, 0, "ScreenSaver");
    qmlRegisterType<MpvObject>("com.stremio.libmpv", 1, 0, "MpvObject");
    qmlRegisterType<RazerChroma>("com.stremio.razerchroma", 1, 0, "RazerChroma");
    qmlRegisterType<ClipboardProxy>("com.stremio.clipboard", 1, 0, "Clipboard");

    ModSettingsDialog modSettings;
    InitializeParameters(engine, app, &modSettings);

    engine->load(QUrl(QStringLiteral("qrc:/main.qml")));

    QObject *rootObject = engine->rootObjects().value(0);
    QObject::connect(&app, SIGNAL(receivedMessage(QVariant,QVariant)), rootObject, SLOT(onAppMessageReceived(QVariant,QVariant)));

    // When reloadRequested is triggered from dialog, we re-load the webview
    QObject::connect(&modSettings, &ModSettingsDialog::reloadRequested, [rootObject]() {
        // We emit a signal to QML that can be handled by Connections to reload the UI
        QMetaObject::invokeMethod(rootObject, "onReloadRequested");
    });

    int ret = app.exec();
    delete engine;
    engine = nullptr;
    return ret;
}

#include "main.moc"
