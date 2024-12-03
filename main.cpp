#include <QQmlApplicationEngine>
#include <QtWebEngine>
#include <QSysInfo>
#include <QDir>
#include <QMap>
#include <QDebug>

#include <clocale>

#define APP_TITLE "Stremio - Freedom to Stream"

#define DESKTOP true

#ifdef DESKTOP
#include <QtWidgets/QApplication>
typedef QApplication Application;

#include <QQmlEngine>

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

#else
#include <QGuiApplication>
#endif

class CssLoader : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList themeNames READ themeNames NOTIFY themeNamesChanged)
    Q_PROPERTY(QString currentThemeContent READ currentThemeContent NOTIFY currentThemeContentChanged)

public:
    CssLoader(QObject *parent = nullptr) : QObject(parent) {
        QDir dir("/home/ras/ephemeral/mods/themes");
        QStringList nameFilters;
        nameFilters << "*.css";
        QFileInfoList fileList = dir.entryInfoList(nameFilters, QDir::Files);
        foreach(QFileInfo fileInfo, fileList) {
            m_themeNames << fileInfo.baseName();
            m_themeFiles[fileInfo.baseName()] = fileInfo.absoluteFilePath();
        }
        m_currentThemeContent = "";
    }

    QStringList themeNames() const {
        return m_themeNames;
    }

    QString currentThemeContent() const {
        return m_currentThemeContent;
    }

public slots:
    void loadTheme(QString themeName) {
        if (m_themeFiles.contains(themeName)) {
            QFile file(m_themeFiles[themeName]);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                m_currentThemeContent = file.readAll();
                file.close();
                emit currentThemeContentChanged();
            } else {
                qWarning() << "Could not open theme file:" << m_themeFiles[themeName];
            }
        } else {
            qWarning() << "Theme not found:" << themeName;
        }
    }

signals:
    void themeNamesChanged();
    void currentThemeContentChanged();

private:
    QStringList m_themeNames;
    QMap<QString, QString> m_themeFiles; // Map themeName to file path
    QString m_currentThemeContent;
};

class JsLoader : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList modNames READ modNames NOTIFY modNamesChanged)
    Q_PROPERTY(QString currentModContent READ currentModContent NOTIFY currentModContentChanged)

public:
    JsLoader(QObject *parent = nullptr) : QObject(parent) {
        QDir dir("/home/ras/ephemeral/mods/mods");
        QStringList nameFilters;
        nameFilters << "*.js";
        QFileInfoList fileList = dir.entryInfoList(nameFilters, QDir::Files);
        foreach(QFileInfo fileInfo, fileList) {
            m_modNames << fileInfo.baseName();
            m_modFiles[fileInfo.baseName()] = fileInfo.absoluteFilePath();
        }
        m_currentModContent = "";
    }

    QStringList modNames() const {
        return m_modNames;
    }

    QString currentModContent() const {
        return m_currentModContent;
    }

public slots:
    void loadMod(QString modName) {
        if (m_modFiles.contains(modName)) {
            QFile file(m_modFiles[modName]);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                m_currentModContent = file.readAll();
                file.close();
                emit currentModContentChanged();
            } else {
                qWarning() << "Could not open mod file:" << m_modFiles[modName];
            }
        } else {
            qWarning() << "Mod not found:" << modName;
        }
    }

signals:
    void modNamesChanged();
    void currentModContentChanged();

private:
    QStringList m_modNames;
    QMap<QString, QString> m_modFiles; // Map modName to file path
    QString m_currentModContent;
};

void InitializeParameters(QQmlApplicationEngine *engine, MainApp& app) {
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
}

int main(int argc, char **argv)
{
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--autoplay-policy=no-user-gesture-required");
    #ifdef _WIN32
    // Default to ANGLE (DirectX), because that seems to eliminate so many issues on Windows
    // Also, according to the docs here: https://wiki.qt.io/Qt_5_on_Windows_ANGLE_and_OpenGL, ANGLE is also preferrable
    // We do not need advanced OpenGL features but we need more universal support

    Application::setAttribute(Qt::AA_UseOpenGLES);
    auto winVer = QSysInfo::windowsVersion();
    if(winVer <= QSysInfo::WV_WINDOWS8 && winVer != QSysInfo::WV_None) {
        qputenv("NODE_SKIP_PLATFORM_CHECK", "1");
    }
    if(winVer <= QSysInfo::WV_WINDOWS7 && winVer != QSysInfo::WV_None) {
        qputenv("QT_ANGLE_PLATFORM", "d3d9");
    }
    #endif

    // This is really broken on Linux
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
        //app.sendMessage( app.arguments().join(' ').toUtf8() );
        return 0;
    }
    #endif

    app.setWindowIcon(QIcon(":/images/stremio_window.png"));


    // Qt sets the locale in the QGuiApplication constructor, but libmpv
    // requires the LC_NUMERIC category to be set to "C", so change it back.
    std::setlocale(LC_NUMERIC, "C");


    static QQmlApplicationEngine* engine = new QQmlApplicationEngine();

    qmlRegisterType<Process>("com.stremio.process", 1, 0, "Process");
    qmlRegisterType<ScreenSaver>("com.stremio.screensaver", 1, 0, "ScreenSaver");
    qmlRegisterType<MpvObject>("com.stremio.libmpv", 1, 0, "MpvObject");
    qmlRegisterType<RazerChroma>("com.stremio.razerchroma", 1, 0, "RazerChroma");
    qmlRegisterType<ClipboardProxy>("com.stremio.clipboard", 1, 0, "Clipboard");

    InitializeParameters(engine, app);

    engine->load(QUrl(QStringLiteral("qrc:/main.qml")));

    #ifndef Q_OS_MACOS
    QObject::connect( &app, &SingleApplication::receivedMessage, &app, &MainApp::processMessage );
    #endif
    QObject::connect( &app, SIGNAL(receivedMessage(QVariant, QVariant)), engine->rootObjects().value(0),
                      SLOT(onAppMessageReceived(QVariant, QVariant)) );
    int ret = app.exec();
    delete engine;
    engine = nullptr;
    return ret;
}

#include "main.moc" // Add this line at the end of the file
