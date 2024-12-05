// main.cpp
#include <QQmlApplicationEngine>
#include <QtWebEngine>
#include <QSysInfo>
#include <clocale>

#define APP_TITLE "Stremio Modshell"
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
#include <QDir>
#include <QFileInfoList>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#else
#include <QGuiApplication>
#endif

class CssLoader : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString cssContent READ cssContent NOTIFY cssContentChanged)
public:
    CssLoader(QObject *parent = nullptr) : QObject(parent) {
        loadCss();
    }

    QString cssContent() const {
        return m_cssContent;
    }

    Q_INVOKABLE void reload() {
        loadCss();
        emit cssContentChanged();
    }

signals:
    void cssContentChanged();

private:
    QString m_cssContent;
    void loadCss() {
        QString basePath = "/home/ras/ephemeral/inject/themes";
        QDir dir(basePath);
        if (!dir.exists()) {
            qWarning() << "Themes directory does not exist:" << basePath;
        }

        QStringList filters;
        filters << "*.css";
        dir.setNameFilters(filters);

        QStringList files = dir.entryList(QDir::Files);
        QString allCss;
        for (const QString &fileName : files) {
            QFile file(dir.filePath(fileName));
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                allCss.append(file.readAll());
                allCss.append("\n");
                file.close();
            } else {
                qWarning() << "Could not open CSS file:" << fileName;
            }
        }
        m_cssContent = allCss;
    }
};

class JsLoader : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString jsContent READ jsContent NOTIFY jsContentChanged)
public:
    JsLoader(QObject *parent = nullptr) : QObject(parent) {
        loadJs();
    }

    QString jsContent() const {
        return m_jsContent;
    }

    Q_INVOKABLE void reload() {
        loadJs();
        emit jsContentChanged();
    }

signals:
    void jsContentChanged();

private:
    QString m_jsContent;
    void loadJs() {
        QString basePath = "/home/ras/ephemeral/inject/mods";
        QDir dir(basePath);
        if (!dir.exists()) {
            qWarning() << "Mods directory does not exist:" << basePath;
        }

        QStringList filters;
        filters << "*.js";
        dir.setNameFilters(filters);

        QStringList files = dir.entryList(QDir::Files);
        QString allJs;
        for (const QString &fileName : files) {
            QFile file(dir.filePath(fileName));
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                allJs.append(file.readAll());
                allJs.append("\n");
                file.close();
            } else {
                qWarning() << "Could not open JS file:" << fileName;
            }
        }

        m_jsContent = allJs;
    }
};

// A helper class to manage mod downloading and reloading
class ModManager : public QObject {
    Q_OBJECT
public:
    ModManager(QObject *parent = nullptr) : QObject(parent) {
        manager = new QNetworkAccessManager(this);
    }

    void setJsLoader(JsLoader *jsLoader) {
        m_jsLoader = jsLoader;
    }

    void setCssLoader(CssLoader *cssLoader) {
        m_cssLoader = cssLoader;
    }

public slots:
    void downloadMod(const QString &url, const QString &id) {
        // download the file from url and save to /home/ras/ephemeral/inject/mods
        QUrl qurl(url);
        if (!qurl.isValid()) {
            qWarning() << "Invalid URL:" << url;
            return;
        }
        QString basePath = "/home/ras/ephemeral/inject/mods";
        QDir dir(basePath);
        if (!dir.exists()) {
            dir.mkpath(basePath);
        }

        // The mod file name would be something like id.js
        QString filePath = dir.filePath(id + ".js");

        QNetworkRequest request(qurl);
        QNetworkReply *reply = manager->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, filePath, id]() {
            if (reply->error() != QNetworkReply::NoError) {
                qWarning() << "Download error:" << reply->errorString();
                reply->deleteLater();
                return;
            }

            QByteArray data = reply->readAll();
            QFile file(filePath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.write(data);
                file.close();
                qDebug() << "Mod saved to:" << filePath;
            } else {
                qWarning() << "Failed to write mod file:" << filePath;
            }
            reply->deleteLater();
            // notify QML/JS that mod is downloaded
            emit modDownloaded(id);
        });
    }

    void reloadMods() {
        // Just call reload on jsLoader and cssLoader
        if (m_jsLoader) m_jsLoader->reload();
        if (m_cssLoader) m_cssLoader->reload();
        emit modsReloaded();
    }

signals:
    void modDownloaded(const QString &id);
    void modsReloaded();

private:
    QNetworkAccessManager *manager;
    JsLoader *m_jsLoader = nullptr;
    CssLoader *m_cssLoader = nullptr;
};

void InitializeParameters(QQmlApplicationEngine *engine, MainApp& app, JsLoader *jsLoader, CssLoader *cssLoader, ModManager *modManager) {
    QQmlContext *ctx = engine->rootContext();
    SystemTray * systemTray = new SystemTray();

    ctx->setContextProperty("applicationDirPath", QGuiApplication::applicationDirPath());
    ctx->setContextProperty("appTitle", QString(APP_TITLE));
    ctx->setContextProperty("autoUpdater", app.autoupdater);
    ctx->setContextProperty("systemTray", systemTray);

    #ifdef QT_DEBUG
        ctx->setContextProperty("debug", true);
    #else
        ctx->setContextProperty("debug", false);
    #endif

    ctx->setContextProperty("cssLoader", cssLoader);
    ctx->setContextProperty("jsLoader", jsLoader);
    ctx->setContextProperty("modManager", modManager);
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

    CssLoader *cssLoader = new CssLoader();
    JsLoader *jsLoader = new JsLoader();
    ModManager *modManager = new ModManager();
    modManager->setJsLoader(jsLoader);
    modManager->setCssLoader(cssLoader);

    InitializeParameters(engine, app, jsLoader, cssLoader, modManager);

    engine->load(QUrl(QStringLiteral("qrc:/main.qml")));

    #ifndef Q_OS_MACOS
    QObject::connect( &app, &SingleApplication::receivedMessage, &app, &MainApp::processMessage );
    #endif
    QObject::connect( &app, SIGNAL(receivedMessage(QVariant, QVariant)), engine->rootObjects().value(0),
                      SLOT(onAppMessageReceived(QVariant, QVariant)) );

    return app.exec();
}

#include "main.moc"
