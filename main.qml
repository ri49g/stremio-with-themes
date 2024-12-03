import QtQuick 2.7
import QtWebEngine 1.4
import QtWebChannel 1.0
import QtQuick.Window 2.2
import QtQuick.Controls 1.4
import QtQuick.Dialogs 1.2
import com.stremio.process 1.0
import com.stremio.screensaver 1.0
import com.stremio.libmpv 1.0
import com.stremio.clipboard 1.0
import QtQml 2.2

import "autoupdater.js" as Autoupdater

ApplicationWindow {
    id: root
    visible: true

    minimumWidth: 1000
    minimumHeight: 650

    readonly property int initialWidth: Math.max(root.minimumWidth, Math.min(1600, Screen.desktopAvailableWidth * 0.8))
    readonly property int initialHeight: Math.max(root.minimumHeight, Math.min(1000, Screen.desktopAvailableHeight * 0.8))

    width: root.initialWidth
    height: root.initialHeight

    property bool quitting: false
    color: "#0c0b11"
    title: appTitle

    property var previousVisibility: Window.Windowed
    property bool wasFullScreen: false

    property string currentTheme: "theme.css"
    property var enabledMods: []

    function setFullScreen(fullscreen) {
        if (fullscreen) {
            root.visibility = Window.FullScreen;
            root.wasFullScreen = true;
        } else {
            root.visibility = root.previousVisibility;
            root.wasFullScreen = false;
        }
    }

    function showWindow() {
        if (root.wasFullScreen) {
            root.visibility = Window.FullScreen;
        } else {
            root.visibility = root.previousVisibility;
        }
        root.raise();
        root.requestActivate();
    }

    function updatePreviousVisibility() {
        if (root.visible && root.visibility != Window.FullScreen && root.visibility != Window.Minimized) {
            root.previousVisibility = root.visibility;
        }
    }

    function onWindowMode(mode) {
        shouldDisableScreensaver(mode === "player")
    }

    function wakeupEvent() {
        shouldDisableScreensaver(true)
        timerScreensaver.restart()
    }

    function shouldDisableScreensaver(condition) {
        if (condition === screenSaver.disabled) return;
        condition ? screenSaver.disable() : screenSaver.enable();
        screenSaver.disabled = condition;
    }

    function isPlayerPlaying() {
        return root.visible && typeof(mpv.getProperty("path"))==="string" && !mpv.getProperty("pause")
    }

    function onAppMessageReceived(instance, message) {
        message = message.toString();
        showWindow();
        if (message !== "SHOW") {
            onAppOpenMedia(message);
        }
    }

    function onAppOpenMedia(message) {
        var url = (message.indexOf('://') > -1 || message.indexOf('magnet:') === 0) ? message : 'file://'+message;
        transport.queueEvent("open-media", url)
    }

    function quitApp() {
        root.quitting = true;
        webView.destroy();
        systemTray.hideIconTray();
        streamingServer.kill();
        streamingServer.waitForFinished(1500);
        Qt.quit();
    }

    Connections {
        target: systemTray
        function onSignalIconMenuAboutToShow() {
            systemTray.updateIsOnTop((root.flags & Qt.WindowStaysOnTopHint) === Qt.WindowStaysOnTopHint);
            systemTray.updateVisibleAction(root.visible);
        }
        function onSignalShow() {
            if(root.visible) {
                root.hide();
            } else {
                showWindow();
            }
        }
        function onSignalAlwaysOnTop() {
            root.raise()
            if (root.flags & Qt.WindowStaysOnTopHint) {
                root.flags &= ~Qt.WindowStaysOnTopHint;
            } else {
                root.flags |= Qt.WindowStaysOnTopHint;
            }
        }
        function onSignalQuit() {
            quitApp();
        }
        function onSignalIconActivated() {
           showWindow();
       }
    }

    ScreenSaver {
        id: screenSaver
        property bool disabled: false
    }

    Timer {
        id: timerScreensaver
        interval: 300000
        running: false
        onTriggered: function () { shouldDisableScreensaver(isPlayerPlaying()) }
    }

    Clipboard {
        id: clipboard
    }

    Process {
        id: streamingServer
        property string errMessage:
            "Error while starting streaming server. Please try to restart stremio. If it happens again please contact the Stremio support team for assistance"
        property int errors: 0
        property bool fastReload: false

        onStarted: function() { stayAliveStreamingServer.stop() }
        onFinished: function(code, status) {
            if (!streamingServer.fastReload && errors < 5 && (code !== 0 || status !== 0) && !root.quitting) {
                transport.queueEvent("server-crash", {"code": code, "log": streamingServer.getErrBuff()});
                errors++
                showStreamingServerErr(code)
            }

            if (streamingServer.fastReload) {
                console.log("streaming server: performing fast re-load")
                streamingServer.fastReload = false
                root.launchServer()
            } else {
                stayAliveStreamingServer.start()
            }
        }
        onAddressReady: function (address) {
            transport.serverAddress = address
            transport.event("server-address", address)
        }
        onErrorThrown: function (error) {
            if (root.quitting) return;
            if (streamingServer.fastReload && error == 1) return;
            transport.queueEvent("server-crash", {"code": error, "log": streamingServer.getErrBuff()});
            showStreamingServerErr(error)
       }
    }

    function showStreamingServerErr(code) {
        errorDialog.text = streamingServer.errMessage
        errorDialog.detailedText = 'stremio streaming server has thrown an error \nQProcess::ProcessError code: '
            + code + '\n\n'
            + streamingServer.getErrBuff();
        errorDialog.visible = true
    }
    function launchServer() {
        var node_executable = applicationDirPath + "/node"
        if (Qt.platform.os === "windows") node_executable = applicationDirPath + "/stremio-runtime.exe"
        streamingServer.start(node_executable,
            [applicationDirPath +"/server.js"].concat(Qt.application.arguments.slice(1)),
            "EngineFS server started at "
        )
    }
    Timer {
        id: stayAliveStreamingServer
        interval: 10000
        running: false
        onTriggered: function () { root.launchServer() }
    }

    MpvObject {
        id: mpv
        anchors.fill: parent
        onMpvEvent: function(ev, args) { transport.event(ev, args) }
    }

    function getWebUrl() {
        var params = "?loginFlow=desktop"
        var args = Qt.application.arguments
        var shortVer = Qt.application.version.split('.').slice(0, 2).join('.')

        var webuiArg = "--webui-url="
        for (var i=0; i!=args.length; i++) {
            if (args[i].indexOf(webuiArg) === 0) return args[i].slice(webuiArg.length)
        }

        if (args.indexOf("--development") > -1 || debug)
            return "http://127.0.0.1:11470/#"+params

        if (args.indexOf("--staging") > -1)
            return "https://staging.strem.io/#"+params

        return "https://app.strem.io/shell-v"+shortVer+"/#"+params;
    }

    Timer {
        id: retryTimer
        interval: 1000
        running: false
        onTriggered: function () {
            webView.tries++
            webView.url = webView.mainUrl;
        }
    }

    function injectJS() {
        splashScreen.visible = false
        pulseOpacity.running = false
        removeSplashTimer.running = false
        webView.webChannel.registerObject( 'transport', transport )

        var cssContent = cssLoader.cssContent.replace(/\\/g, '\\\\').replace(/'/g, "\\'").replace(/\n/g, "\\n");
        var jsContent = jsLoader.jsContent.replace(/\\/g, '\\\\').replace(/'/g, "\\'").replace(/\n/g, "\\n");

        var injectedJS = "try { initShellComm(); " +
            "var style = document.createElement('style'); style.innerHTML = '" + cssContent + "'; document.head.appendChild(style); " +
            "var script = document.createElement('script'); script.innerHTML = '" + jsContent + "'; document.head.appendChild(script); " +
            "} catch(e) { setTimeout(function() { throw e }); }"

        webView.runJavaScript(injectedJS, function(err) {
            if (err) {
                errorDialog.text = "user interface could not be loaded.\n\nplease try again later or contact the stremio support team for assistance."
                errorDialog.detailedText = err
                errorDialog.visible = true
                console.log(err)
            }
        });
    }

    function reloadInjectedContent() {
        jsLoader.setMods(enabledMods)
        cssLoader.setTheme(currentTheme)
        injectJS()
    }

    Timer {
        id: removeSplashTimer
        interval: 90000
        running: true
        repeat: false
        onTriggered: function () {
            webView.backgroundColor = "transparent"
            injectJS()
        }
    }

    WebEngineView {
        id: webView;

        focus: true

        readonly property string mainUrl: getWebUrl()

        url: webView.mainUrl;
        anchors.fill: parent
        backgroundColor: "transparent";
        property int tries: 0
        readonly property int maxTries: 20

        Component.onCompleted: function() {
            console.log("loading web ui from url: "+webView.mainUrl)
            webView.profile.httpUserAgent = webView.profile.httpUserAgent+' stremioshell/'+Qt.application.version
            webView.profile.httpCacheMaximumSize = 209715200
        }

        onLoadingChanged: function(loadRequest) {
            webView.backgroundColor = "transparent"
            var successfullyLoaded = loadRequest.status == WebEngineView.LoadSucceededStatus
            if (successfullyLoaded || webView.tries > 0) {
                splashScreen.visible = false
                pulseOpacity.running = false
            }

            if (successfullyLoaded) {
                injectJS()
            }

            var shouldRetry = loadRequest.status == WebEngineView.LoadFailedStatus ||
                              loadRequest.status == WebEngineView.LoadStoppedStatus
            if ( shouldRetry && webView.tries < webView.maxTries) {
                retryTimer.restart()
            }
        }

        onRenderProcessTerminated: function(terminationStatus, exitCode) {
            console.log("render process terminated with code "+exitCode+" and status: "+terminationStatus)
            webView.backgroundColor = "black"
            retryTimer.restart()
            transport.queued = []
            transport.queueEvent("render-process-terminated", { exitCode: exitCode, terminationStatus: terminationStatus, url: webView.url })
        }

        // remove hoveredUrl handling

        onFullScreenRequested: function(req) {
            setFullScreen(req.toggleOn);
            req.accept();
        }

        onNavigationRequested: function(req) {
            var allowedHost = webView.mainUrl.split('/')[2]
            var targetHost = req.url.toString().split('/')[2]
            if (allowedHost != targetHost && (req.isMainFrame || targetHost !== 'www.youtube.com')) {
                 console.log("onNavigationRequested: disallowed url "+req.url.toString());
                 req.action = WebEngineView.IgnoreRequest;
            }
        }

        // no context menu modifications needed here
        DropArea {
            anchors.fill: parent
            onDropped: function(dropargs){
                var args = JSON.parse(JSON.stringify(dropargs))
                transport.event("dragdrop", args.urls)
            }
        }
        webChannel: wChannel
    }

    WebChannel {
        id: wChannel
    }

    Rectangle {
        id: splashScreen;
        color: "#0c0b11";
        anchors.fill: parent;
        Image {
            id: splashLogo
            source: "qrc:///images/stremio.png"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter

            SequentialAnimation {
                id: pulseOpacity
                running: true
                NumberAnimation { target: splashLogo; property: "opacity"; to: 1.0; duration: 600; easing.type: Easing.Linear; }
                NumberAnimation { target: splashLogo; property: "opacity"; to: 0.3; duration: 600; easing.type: Easing.Linear; }
                loops: Animation.Infinite
            }
        }
    }

    MessageDialog {
        id: errorDialog
        title: "stremio - application error"
    }

    FileDialog {
      id: fileDialog
      folder: shortcuts.home
      onAccepted: {
        var fileProtocol = "file://"
        var onWindows = Qt.platform.os === "windows" ? 1 : 0
        var pathSeparators = ["/", "\\"]
        var files = fileDialog.fileUrls.filter(function(fileUrl) {
          return fileUrl.startsWith(fileProtocol)
        })
        .map(function(fileUrl) {
          return decodeURIComponent(fileUrl
            .substring(fileProtocol.length + onWindows))
            .replace(/\//g, pathSeparators[onWindows])
        })
        transport.event("file-selected", {
          files: files,
          title: fileDialog.title,
          selectExisting: fileDialog.selectExisting,
          selectFolder: fileDialog.selectFolder,
          selectMultiple: fileDialog.selectMultiple,
          nameFilters: fileDialog.nameFilters,
          selectedNameFilter: fileDialog.selectedNameFilter,
          data: fileDialog.data
        })
      }
      onRejected: {
        transport.event("file-rejected", {
          title: fileDialog.title,
          selectExisting: fileDialog.selectExisting,
          selectFolder: fileDialog.selectFolder,
          selectMultiple: fileDialog.selectMultiple,
          nameFilters: fileDialog.nameFilters,
          selectedNameFilter: fileDialog.selectedNameFilter,
          data: fileDialog.data
        })
      }
      property var data: {}
    }

    onWindowStateChanged: function(state) {
        updatePreviousVisibility();
        transport.event("win-state-changed", { state: state })
    }

    onVisibilityChanged: {
        var enabledAlwaysOnTop = root.visible && root.visibility != Window.FullScreen;
        systemTray.alwaysOnTopEnabled(enabledAlwaysOnTop);
        if (!enabledAlwaysOnTop) {
            root.flags &= ~Qt.WindowStaysOnTopHint;
        }
        updatePreviousVisibility();
        transport.event("win-visibility-changed", { visible: root.visible, visibility: root.visibility,
                            isFullscreen: root.visibility === Window.FullScreen })
    }

    property int appState: Qt.application.state;
    onAppStateChanged: {
        var clipboardUrl
        if (clipboard.text.match(/^(magnet|http|https|file|stremio|ipfs):/)) clipboardUrl = clipboard.text
        transport.event("app-state-changed", { state: appState, clipboard: clipboardUrl })

        if (Qt.platform.os === "osx" && appState === Qt.ApplicationActive && !root.visible) {
            root.show()
        }
    }

    onClosing: function(event){
        event.accepted = false
        root.hide()
    }

    signal autoUpdaterErr(var msg, var err);
    signal autoUpdaterRestartTimer();

    Timer {
        id: autoUpdaterLongTimer
        interval: 2 * 60 * 60 * 1000
        running: false
        onTriggered: function() { autoUpdaterShortTimer.restart() }
    }
    Timer {
        id: autoUpdaterShortTimer
        interval: 5 * 60 * 1000
        running: false
        onTriggered: function () { }
    }

    Component.onCompleted: function() {
        console.log('stremio shell version: '+Qt.application.version)
        root.height = root.initialHeight
        root.width = root.initialWidth

        var args = Qt.application.arguments
        if (args.indexOf("--development") > -1 && args.indexOf("--streaming-server") === -1)
            console.log("skipping launch of streaming server under --development");
        else
            launchServer();

        var lastArg = args[1];
        if (args.length > 1 && !lastArg.match('^--')) onAppOpenMedia(lastArg)

        console.info(" **** completed. loading autoupdater ***")
        Autoupdater.initAutoUpdater(autoUpdater, root.autoUpdaterErr, autoUpdaterShortTimer, autoUpdaterLongTimer, autoUpdaterRestartTimer, webView.profile.httpUserAgent);
    }

    Shortcut {
        sequence: "Alt"
        onActivated: {
            altPopup.visible = true
        }
    }

    Popup {
        id: altPopup
        width: 400
        height: 600
        focus: true
        closePolicy: Popup.CloseOnEscape
        contentItem: Rectangle {
            color: "#333"
            anchors.fill: parent

            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                Text {
                    text: "themes"
                    color: "#fff"
                }
                Column {
                    Repeater {
                        model: modManager.themes
                        RadioButton {
                            text: modelData
                            checked: modelData === currentTheme
                            onClicked: {
                                currentTheme = modelData
                                reloadInjectedContent()
                            }
                        }
                    }
                }

                Text {
                    text: "mods"
                    color: "#fff"
                }
                Column {
                    Repeater {
                        model: modManager.mods
                        CheckBox {
                            text: modelData
                            checked: enabledMods.indexOf(modelData) !== -1
                            onClicked: {
                                var idx = enabledMods.indexOf(modelData)
                                if (idx === -1) {
                                    enabledMods.push(modelData)
                                } else {
                                    enabledMods.splice(idx,1)
                                }
                                reloadInjectedContent()
                            }
                        }
                    }
                }

                Button {
                    text: "refresh lists"
                    onClicked: {
                        modManager.refresh()
                    }
                }

                Button {
                    text: "close"
                    onClicked: altPopup.visible = false
                }
            }
        }
    }
}
