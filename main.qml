import QtQuick 2.7
import QtWebEngine 1.4
import QtWebChannel 1.0
import QtQuick.Window 2.2 // for Window instead of ApplicationWindow; also for Screen
import QtQuick.Controls 1.4 // for ApplicationWindow
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

    color: "#0c0b11";
    title: appTitle

    property var previousVisibility: Window.Windowed
    property bool wasFullScreen: false

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

    // Transport
    QtObject {
        id: transport
        readonly property string shellVersion: Qt.application.version
        property string serverAddress: "http://127.0.0.1:11470" // will be set to something else if server inits on another port

        readonly property bool isFullscreen: root.visibility === Window.FullScreen // just to send the initial state

        signal event(var ev, var args)
        function onEvent(ev, args) {
            if (ev === "quit") quitApp()
            if (ev === "app-ready") transport.flushQueue()
            if (ev === "mpv-command" && args && args[0] !== "run") mpv.command(args)
            if (ev === "mpv-set-prop") {
                mpv.setProperty(args[0], args[1]);
                if (args[0] === "pause") {
                    shouldDisableScreensaver(!args[1]);
                }
            }
            if (ev === "mpv-observe-prop") mpv.observeProperty(args)
            if (ev === "control-event") wakeupEvent()
            if (ev === "wakeup") wakeupEvent()
            if (ev === "set-window-mode") onWindowMode(args)
            if (ev === "open-external") Qt.openUrlExternally(args)
            if (ev === "win-focus" && !root.visible) {
                showWindow();
            }
            if (ev === "win-set-visibility") {
                if (args.hasOwnProperty('fullscreen')) {
                    setFullScreen(args.fullscreen);
                }
            }
            if (ev === "autoupdater-notif-clicked" && autoUpdater.onNotifClicked) {
                autoUpdater.onNotifClicked();
            }
            //if (ev === "chroma-toggle") { args.enabled ? chroma.enable() : chroma.disable() }
            if (ev === "screensaver-toggle") shouldDisableScreensaver(args.disabled)
            if (ev === "file-close") fileDialog.close()
            if (ev === "file-open") {
                if (typeof args !== "undefined") {
                    var fileDialogDefaults = {
                        title: "Please choose",
                        selectExisting: true,
                        selectFolder: false,
                        selectMultiple: false,
                        nameFilters: [],
                        selectedNameFilter: "",
                        data: null
                    }
                    Object.keys(fileDialogDefaults).forEach(function(key) {
                        fileDialog[key] = args.hasOwnProperty(key) ? args[key] : fileDialogDefaults[key]
                    })
                }
                fileDialog.open()
            }
            if (ev === "load-theme") {
                cssLoader.loadTheme(args);
                injectTheme();
            }
            if (ev === "load-mod") {
                jsLoader.loadMod(args);
                injectMod();
            }
            if (ev === "unload-mod") {
                jsLoader.unloadMod(args);
                injectMod(); // Re-inject the current mods
            }
        }

        // events that we want to wait for the app to initialize
        property variant queued: []
        function queueEvent() {
            if (transport.queued) transport.queued.push(arguments)
            else transport.event.apply(transport, arguments)
        }
        function flushQueue() {
            if (transport.queued) transport.queued.forEach(function(args) { transport.event.apply(transport, args) })
            transport.queued = null;
        }
    }

    // Utilities
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

    // Received external message
    function onAppMessageReceived(instance, message) {
        message = message.toString(); // cause it may be QUrl
        showWindow();
        if (message !== "SHOW") {
            onAppOpenMedia(message);
        }
    }

    // May be called from a message (from another app instance) or when app is initialized with an arg
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

    /* With help Connections object
     * set connections with System tray class
     * */
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

        // The signal - close the application by ignoring the check-box
        function onSignalQuit() {
            quitApp();
        }

        // Minimize / maximize the window by clicking on the default system tray
        function onSignalIconActivated() {
           showWindow();
       }
    }

    // Screen saver - enable & disable
    ScreenSaver {
        id: screenSaver
        property bool disabled: false // track last state so we don't call it multiple times
    }
    // This is needed so that 300s after the remote control has been used, we can re-enable the screensaver
    // (if the player is not playing)
    Timer {
        id: timerScreensaver
        interval: 300000
        running: false
        onTriggered: function () { shouldDisableScreensaver(isPlayerPlaying()) }
    }

    // Clipboard proxy
    Clipboard {
        id: clipboard
    }

    //
    // Streaming server
    //
    Process {
        id: streamingServer
        property string errMessage:
            "Error while starting streaming server. Please try to restart stremio. If it happens again please contact the Stremio support team for assistance"
        property int errors: 0
        property bool fastReload: false

        onStarted: function() { stayAliveStreamingServer.stop() }
        onFinished: function(code, status) {
            // status -> QProcess::CrashExit is 1
            if (!streamingServer.fastReload && errors < 5 && (code !== 0 || status !== 0) && !root.quitting) {
                transport.queueEvent("server-crash", {"code": code, "log": streamingServer.getErrBuff()});

                errors++
                showStreamingServerErr(code)
            }

            if (streamingServer.fastReload) {
                console.log("Streaming server: performing fast re-load")
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
            if (root.quitting) return; // inhibits errors during quitting
            if (streamingServer.fastReload && error == 1) return; // inhibit errors during fast reload mode;
                                                                  // we'll unset that after we've restarted the server
            transport.queueEvent("server-crash", {"code": error, "log": streamingServer.getErrBuff()});
            showStreamingServerErr(error)
       }
    }
    function showStreamingServerErr(code) {
        errorDialog.text = streamingServer.errMessage
        errorDialog.detailedText = 'Stremio streaming server has thrown an error \nQProcess::ProcessError code: '
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
    // TimerStreamingServer
    Timer {
        id: stayAliveStreamingServer
        interval: 10000
        running: false
        onTriggered: function () { root.launchServer() }
    }

    //
    // Player
    //
    MpvObject {
        id: mpv
        anchors.fill: parent
        onMpvEvent: function(ev, args) { transport.event(ev, args) }
    }

    //
    // Main UI (via WebEngineView)
    //
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

    function injectTheme() {
        var cssContent = cssLoader.currentThemeContent.replace(/\\/g, '\\\\').replace(/'/g, "\\'").replace(/\n/g, "\\n");
        var jsCode = `
            var style = document.getElementById('dynamic-theme');
            if (style) {
                style.innerHTML = '${cssContent}';
            } else {
                style = document.createElement('style');
                style.id = 'dynamic-theme';
                style.innerHTML = '${cssContent}';
                document.head.appendChild(style);
            }
        `;
        webView.runJavaScript(jsCode);
    }

    function injectMod() {
        var jsContent = jsLoader.currentModContent.replace(/\\/g, '\\\\').replace(/'/g, "\\'").replace(/\n/g, "\\n");
        var jsCode = `
            var script = document.getElementById('dynamic-mod');
            if (script) {
                script.innerHTML = '${jsContent}';
            } else {
                script = document.createElement('script');
                script.id = 'dynamic-mod';
                script.innerHTML = '${jsContent}';
                document.head.appendChild(script);
            }
        `;
        webView.runJavaScript(jsCode);
    }

    function injectCustomMenuJS() {
        var jsCode = `
            (function() {
                function addCustomSettings() {
                    // Check if settings page is available
                    var settingsNav = document.querySelector("#settingsPage > div.sections > nav");
                    var settingsPanel = document.querySelector("#settingsPanel");
                    if (!settingsNav || !settingsPanel) {
                        // Try again after a delay
                        setTimeout(addCustomSettings, 500);
                        return;
                    }

                    // Add new section to the navigation
                    var sectionId = "custom";
                    var sectionTitle = "Custom Settings";
                    var navItem = document.createElement("a");
                    navItem.href = "#settings-" + sectionId;
                    navItem.tabIndex = -1;
                    navItem.innerHTML = '<div class="label ng-scope ng-binding">' + sectionTitle + '</div>';
                    settingsNav.appendChild(navItem);

                    // Add new section to the settings panel
                    var section = document.createElement("section");
                    section.id = sectionId;
                    section.innerHTML = '<h2 class="ng-scope ng-binding">' + sectionTitle + '</h2>';
                    settingsPanel.appendChild(section);

                    // Add content to the section
                    // Themes
                    var themesDiv = document.createElement("div");
                    themesDiv.className = "category";
                    themesDiv.innerHTML = '<div class="title">Themes</div>';
                    section.appendChild(themesDiv);

                    var themeList = document.createElement("div");
                    themeList.className = "option option-select";
                    var themeSelect = document.createElement("select");
                    themeSelect.id = "themeSelect";
                    themeSelect.innerHTML = '<option value="">Default</option>';
                    window.themeNames.forEach(function(themeName) {
                        var option = document.createElement("option");
                        option.value = themeName;
                        option.textContent = themeName;
                        themeSelect.appendChild(option);
                    });
                    themeSelect.addEventListener("change", function() {
                        transport.onEvent('load-theme', this.value);
                    });
                    themeList.appendChild(themeSelect);
                    themesDiv.appendChild(themeList);

                    // Mods
                    var modsDiv = document.createElement("div");
                    modsDiv.className = "category";
                    modsDiv.innerHTML = '<div class="title">Mods</div>';
                    section.appendChild(modsDiv);

                    window.modNames.forEach(function(modName) {
                        var modOption = document.createElement("div");
                        modOption.className = "option option-toggle";
                        var checkbox = document.createElement("input");
                        checkbox.type = "checkbox";
                        checkbox.id = "mod-" + modName;
                        checkbox.addEventListener("change", function() {
                            if (this.checked) {
                                transport.onEvent('load-mod', modName);
                            } else {
                                transport.onEvent('unload-mod', modName);
                            }
                        });
                        var label = document.createElement("label");
                        label.htmlFor = "mod-" + modName;
                        label.textContent = modName;
                        modOption.appendChild(checkbox);
                        modOption.appendChild(label);
                        modsDiv.appendChild(modOption);
                    });

                    // Handle navigation clicks
                    navItem.addEventListener('click', function() {
                        document.querySelectorAll("#settingsPage > div.sections > nav > a").forEach(function(a) {
                            a.classList.remove("active");
                        });
                        navItem.classList.add("active");
                        settingsPanel.querySelectorAll("section").forEach(function(sec) {
                            sec.style.display = 'none';
                        });
                        section.style.display = 'block';
                    });
                }

                // Wait for the app to fully load before injecting the menu
                if (document.readyState === 'complete' || document.readyState === 'interactive') {
                    setTimeout(addCustomSettings, 1000);
                } else {
                    document.addEventListener("DOMContentLoaded", function() {
                        setTimeout(addCustomSettings, 1000);
                    });
                }
            })();
        `;
        return jsCode;
    }

    function injectJS() {
        splashScreen.visible = false;
        pulseOpacity.running = false;
        removeSplashTimer.running = false;
        webView.webChannel.registerObject('transport', transport);

        var cssContent = cssLoader.currentThemeContent.replace(/\\/g, '\\\\').replace(/'/g, "\\'").replace(/\n/g, "\\n");
        var jsContent = jsLoader.currentModContent.replace(/\\/g, '\\\\').replace(/'/g, "\\'").replace(/\n/g, "\\n");
        var themeNames = JSON.stringify(cssLoader.themeNames);
        var modNames = JSON.stringify(jsLoader.modNames);

        var injectedJS = `
            try {
                new QWebChannel(qt.webChannelTransport, function(channel) {
                    window.transport = channel.objects.transport;
                    window.themeNames = ${themeNames};
                    window.modNames = ${modNames};
                    ${injectCustomMenuJS()}
                });

                // Inject initial CSS and JS
                var style = document.createElement('style');
                style.id = 'dynamic-theme';
                style.innerHTML = '${cssContent}';
                document.head.appendChild(style);

                var script = document.createElement('script');
                script.id = 'dynamic-mod';
                script.innerHTML = '${jsContent}';
                document.head.appendChild(script);
            } catch(e) {
                console.error(e);
            }
        `;

        webView.runJavaScript(injectedJS, function(result) {
            if (result === undefined) {
                webView.tries = 0;
            } else {
                errorDialog.text = "User Interface could not be loaded.\n\nPlease try again later or contact the Stremio support team for assistance.";
                errorDialog.detailedText = result;
                errorDialog.visible = true;
                console.error(result);
            }
        });
    }

    // We want to remove the splash after a minute
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
            console.log("Loading web UI from URL: "+webView.mainUrl)

            webView.profile.httpUserAgent = webView.profile.httpUserAgent+' StremioShell/'+Qt.application.version

            // for more info, see
            // https://github.com/adobe/chromium/blob/master/net/disk_cache/backend_impl.cc - AdjustMaxCacheSize,
            // https://github.com/adobe/chromium/blob/master/net/disk_cache/backend_impl.cc#L2094
            webView.profile.httpCacheMaximumSize = 209715200 // 200 MB
        }

        onLoadingChanged: function(loadRequest) {
            // hack for webEngineView changing it's background color on crashes
            webView.backgroundColor = "transparent"

            var successfullyLoaded = loadRequest.status == WebEngineView.LoadSucceededStatus
            if (successfullyLoaded || webView.tries > 0) {
                // show the webview if the loading is failing
                // can fail because of many reasons, including captive portals
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

            // hack for webEngineView changing it's background color on crashes
            webView.backgroundColor = "black"

            retryTimer.restart()

            // send an event for the crash, but since the web UI is not working, reset the queue and queue it
            transport.queued = []
            transport.queueEvent("render-process-terminated", { exitCode: exitCode, terminationStatus: terminationStatus, url: webView.url })

        }

        // In the app, we use open-external IPC signal, but make sure this works anyway
        property string hoveredUrl: ""
        onLinkHovered: webView.hoveredUrl = hoveredUrl
        onNewViewRequested: function(req) { if (req.userInitiated) Qt.openUrlExternally(webView.hoveredUrl) }

        // Prevent navigation
        onNavigationRequested: function(req) {
            // WARNING: we should load the app through https to avoid MITM attacks on the clipboard
            var allowedHost = webView.mainUrl.split('/')[2]
            var targetHost = req.url.toString().split('/')[2]
            if (allowedHost != targetHost && (req.isMainFrame || targetHost !== 'www.youtube.com')) {
                 console.log("onNavigationRequested: disallowed URL "+req.url.toString());
                 req.action = WebEngineView.IgnoreRequest;
            }
        }

        Menu {
            id: ctxMenu
            MenuItem {
                text: "Undo"
                shortcut: StandardKey.Undo
                onTriggered: webView.triggerWebAction(WebEngineView.Undo)
            }
            MenuItem {
                text: "Redo"
                shortcut: StandardKey.Redo
                onTriggered: webView.triggerWebAction(WebEngineView.Redo)
            }
            MenuSeparator { }
            MenuItem {
                text: "Cut"
                shortcut: StandardKey.Cut
                onTriggered: webView.triggerWebAction(WebEngineView.Cut)
            }
            MenuItem {
                text: "Copy"
                shortcut: StandardKey.Copy
                onTriggered: webView.triggerWebAction(WebEngineView.Copy)
            }
            MenuItem {
                text: "Paste"
                shortcut: StandardKey.Paste
                onTriggered: webView.triggerWebAction(WebEngineView.Paste)
            }
            MenuSeparator { }
            MenuItem {
                text: "Select All"
                shortcut: StandardKey.SelectAll
                onTriggered: webView.triggerWebAction(WebEngineView.SelectAll)
            }
        }

        // Prevent ctx menu
        onContextMenuRequested: function(request) {
            request.accepted = true;
            // Allow menu inside editable objects
            if (request.isContentEditable) {
                ctxMenu.popup();
            }
        }

        Action {
            shortcut: StandardKey.Paste
            onTriggered: webView.triggerWebAction(WebEngineView.Paste)
        }

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

    //
    // Splash screen
    // Must be over the UI
    //
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
                NumberAnimation { target: splashLogo; property: "opacity"; to: 1.0; duration: 600;
                    easing.type: Easing.Linear; }
                NumberAnimation { target: splashLogo; property: "opacity"; to: 0.3; duration: 600;
                    easing.type: Easing.Linear; }
                loops: Animation.Infinite
            }
        }
    }

    //
    // Err dialog
    //
    MessageDialog {
        id: errorDialog
        title: "Stremio - Application Error"
        // onAccepted handler does not work
        //icon: StandardIcon.Critical
        //standardButtons: StandardButton.Ok
    }

    FileDialog {
      id: fileDialog
      folder: shortcuts.home
      onAccepted: {
        var fileProtocol = "file://"
        var onWindows = Qt.platform.os === "windows" ? 1 : 0
        var pathSeparators = ["/", "\\"]
        var files = fileDialog.fileUrls.filter(function(fileUrl) {
          // Ignore network drives and alike
          return fileUrl.startsWith(fileProtocol)
        })
        .map(function(fileUrl) {
          // Send actual path and not file protocol URL
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

    //
    // Binding window -> app events
    //
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
        // WARNING: we should load the app through https to avoid MITM attacks on the clipboard
        var clipboardUrl
        if (clipboard.text.match(/^(magnet|http|https|file|stremio|ipfs):/)) clipboardUrl = clipboard.text
        transport.event("app-state-changed", { state: appState, clipboard: clipboardUrl })

        // WARNING: CAVEAT: this works when you've focused ANOTHER app and then get back to this one
        if (Qt.platform.os === "osx" && appState === Qt.ApplicationActive && !root.visible) {
            root.show()
        }
    }

    onClosing: function(event){
        event.accepted = false
        root.hide()
    }

    //
    // AUTO UPDATER
    //
    signal autoUpdaterErr(var msg, var err);
    signal autoUpdaterRestartTimer();

    // Explanation: when the long timer expires, we schedule the short timer; we do that,
    // because in case the computer has been asleep for a long time, we want another short timer so we don't check
    // immediately (network not connected yet, etc)
    // we also schedule the short timer if the computer is offline
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
        onTriggered: function () { } // empty, set if auto-updater is enabled in initAutoUpdater()
    }

    //
    // On complete handler
    //
    Component.onCompleted: function() {
        console.log('Stremio Shell version: '+Qt.application.version)

        // Kind of hacky way to ensure there are no Qt bindings going on; otherwise when we go to fullscreen
        // Qt tries to restore original window size
        root.height = root.initialHeight
        root.width = root.initialWidth

        // Start streaming server
        var args = Qt.application.arguments
        if (args.indexOf("--development") > -1 && args.indexOf("--streaming-server") === -1)
            console.log("Skipping launch of streaming server under --development");
        else
            launchServer();

        // Handle file opens
        var lastArg = args[1]; // not actually last, but we want to be consistent with what happens when we open
                               // a second instance (main.cpp)
        if (args.length > 1 && !lastArg.match('^--')) onAppOpenMedia(lastArg)

        // Check for updates
        console.info(" **** Completed. Loading Autoupdater ***")
        Autoupdater.initAutoUpdater(autoUpdater, root.autoUpdaterErr, autoUpdaterShortTimer, autoUpdaterLongTimer, autoUpdaterRestartTimer, webView.profile.httpUserAgent);
    }
}
