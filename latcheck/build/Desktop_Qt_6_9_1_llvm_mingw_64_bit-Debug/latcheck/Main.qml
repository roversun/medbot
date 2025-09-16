import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.VirtualKeyboard
import LatCheck 1.0

ApplicationWindow {
    id: window
    width: 800
    height: 600
    visible: true
    title: "LatCheck v1.0 - Network Latency Checker"
    
    property bool isRunning: latencyChecker.running
    property bool showConfig: false  // 控制配置界面显示
    
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            
            Label {
                text: "LatCheck v1.0"
                font.bold: true
                Layout.fillWidth: true
            }
            
            ToolButton {
                text: showConfig ? "❌" : "⚙️"
                font.pixelSize: 20
                onClicked: showConfig = !showConfig
            }
        }
    }
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 15
        
        // 内嵌配置界面
        GroupBox {
            title: "Configuration"
            Layout.fillWidth: true
            visible: showConfig
            
            ScrollView {
                anchors.fill: parent
                implicitHeight: showConfig ? 400 : 0
                
                ColumnLayout {
                    width: parent.width
                    spacing: 15
                    
                    GroupBox {
                        title: "Server Configuration"
                        Layout.fillWidth: true
                        
                        GridLayout {
                            columns: 2
                            anchors.fill: parent
                            
                            Label { text: "Server IP:" }
                            TextField {
                                id: serverIpField
                                Layout.fillWidth: true
                                placeholderText: "127.0.0.1"
                                text: configManager.serverIp
                            }
                            
                            Label { text: "Server Port:" }
                            SpinBox {
                                id: serverPortField
                                from: 1
                                to: 65535
                                value: configManager.serverPort
                            }
                        }
                    }
                    
                    GroupBox {
                        title: "Threading Configuration"
                        Layout.fillWidth: true
                        
                        GridLayout {
                            columns: 2
                            anchors.fill: parent
                            
                            Label { text: "Thread Count:" }
                            SpinBox {
                                id: threadCountField
                                from: 1
                                to: 200
                                value: configManager.threadCount
                            }
                        }
                    }
                    
                    GroupBox {
                        title: "Authentication"
                        Layout.fillWidth: true
                        
                        GridLayout {
                            columns: 2
                            anchors.fill: parent
                            
                            Label { text: "Username:" }
                            TextField {
                                id: usernameField
                                Layout.fillWidth: true
                                text: configManager.username
                            }
                            
                            Label { text: "Password:" }
                            TextField {
                                id: passwordField
                                Layout.fillWidth: true
                                echoMode: TextInput.Password
                                placeholderText: "Enter password"
                            }
                        }
                    }
                    
                    RowLayout {
                        Layout.fillWidth: true
                        
                        Button {
                            text: "Test Connection"
                            enabled: serverIpField.text.length > 0
                            onClicked: {
                                networkManager.connectToServer(
                                    serverIpField.text, 
                                    serverPortField.value,
                                    "", // cert path - simplified for inline config
                                    ""  // key path - simplified for inline config
                                )
                            }
                        }
                        
                        Button {
                            text: "Login"
                            enabled: networkManager.connected && 
                                    usernameField.text.length > 0 && 
                                    passwordField.text.length > 0
                            onClicked: {
                                if (configManager.setPassword(passwordField.text)) {
                                    networkManager.login(usernameField.text, passwordField.text)
                                }
                            }
                        }
                        
                        Item { Layout.fillWidth: true }
                        
                        Button {
                            text: "Save"
                            onClicked: {
                                configManager.serverIp = serverIpField.text
                                configManager.serverPort = serverPortField.value
                                configManager.threadCount = threadCountField.value
                                configManager.username = usernameField.text
                                
                                if (passwordField.text.length > 0) {
                                    configManager.setPassword(passwordField.text)
                                }
                                
                                configManager.saveConfig()
                                logger.logMessage("Configuration saved successfully.")
                                showConfig = false
                            }
                        }
                    }
                    
                    Label {
                        text: "Connection Status: " + networkManager.connectionStatus
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: networkManager.connected ? "green" : "red"
                    }
                }
            }
        }
        
        // Location Input
        GroupBox {
            title: "Location Information"
            Layout.fillWidth: true
            
            RowLayout {
                anchors.fill: parent
                
                TextField {
                    id: locationField
                    Layout.fillWidth: true
                    placeholderText: "Enter your location"
                    text: configManager.location
                    onTextChanged: configManager.location = text
                }
                
                CheckBox {
                    id: autoLocationCheck
                    text: "Auto Location"
                    enabled: true  // Always enabled now
                    onCheckedChanged: {
                        if (checked) {
                            logger.logMessage("Starting automatic location detection...")
                            locationService.startLocationUpdate()
                            
                            // 如果已经有位置信息，立即更新到TextField
                            if (locationService.currentLocation !== "Unknown" && locationService.currentLocation !== "") {
                                locationField.text = locationService.currentLocation
                                configManager.location = locationService.currentLocation
                                logger.logMessage("Using cached location: " + locationService.currentLocation)
                            }
                        } else {
                            logger.logMessage("Stopping automatic location detection...")
                            locationService.stopLocationUpdate()
                        }
                    }
                }
                
                BusyIndicator {
                    visible: locationService.isUpdating
                    running: locationService.isUpdating
                    width: 20
                    height: 20
                }
            }
        }
        
        // Control Buttons
        RowLayout {
            Layout.fillWidth: true
            
            Button {
                id: startButton
                text: "Start"
                enabled: !isRunning && networkManager.connected
                onClicked: {
                    if (locationField.text.length > 0) {
                        logger.startNewSession(locationField.text)
                        configManager.saveConfig()
                        
                        // Request IP list from server
                        var ipList = networkManager.requestIpList()
                    }
                }
            }
            
            Button {
                id: stopButton
                text: "Stop"
                enabled: isRunning
                onClicked: {
                    latencyChecker.stopChecking()
                    logger.endSession()
                }
            }
            
            Item { Layout.fillWidth: true }
            
            Label {
                text: isRunning ? 
                    `Progress: ${latencyChecker.progress}/${latencyChecker.totalIps}` :
                    "Ready"
            }
        }
        
        // Progress Bar
        ProgressBar {
            Layout.fillWidth: true
            visible: isRunning
            value: latencyChecker.totalIps > 0 ? 
                latencyChecker.progress / latencyChecker.totalIps : 0
        }
        
        // Log Display
        GroupBox {
            title: "Log Output"
            Layout.fillWidth: true
            Layout.fillHeight: true
            
            ScrollView {
                anchors.fill: parent
                
                TextArea {
                    id: logArea
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    selectByMouse: true
                    font.family: "Consolas, Monaco, monospace"
                    
                    text: "LatCheck v1.0 - Ready\n" +
                          "Please configure connection settings and click Start to begin latency checking.\n"
                }
            }
        }
        
        // Status Bar
        Label {
            Layout.fillWidth: true
            text: `Connection: ${networkManager.connectionStatus} | ` +
                  `Log File: ${logger.currentLogFile || "None"}`
            font.pixelSize: 10
            color: "gray"
        }
    }
    
    // 删除这部分 - Configuration Dialog
    // ConfigDialog {
    //     id: configDialog
    //     
    //     onLoginRequested: function(username, password) {
    //         networkManager.login(username, password)
    //     }
    //     
    //     onConfigSaved: {
    //         logArea.append("Configuration saved successfully.")
    //     }
    // }
    
    // Virtual Keyboard
    InputPanel {
        id: inputPanel
        z: 99
        x: 0
        y: window.height
        width: window.width

        states: State {
            name: "visible"
            when: inputPanel.active
            PropertyChanges {
                target: inputPanel
                y: window.height - inputPanel.height
            }
        }
        transitions: Transition {
            from: ""
            to: "visible"
            reversible: true
            ParallelAnimation {
                NumberAnimation {
                    properties: "y"
                    duration: 250
                    easing.type: Easing.InOutQuad
                }
            }
        }
    }
    
    // Connections
    Connections {
        target: networkManager
        
        function onLoginResult(success, message) {
            if (success) {
                logArea.append("Login successful: " + message)
            } else {
                logArea.append("Login failed: " + message)
            }
        }
        
        function onIpListReceived(ipList) {
            logArea.append(`Received ${ipList.length} IP addresses from server`)
            latencyChecker.startChecking(ipList, configManager.threadCount)
        }
        
        function onErrorOccurred(error) {
            logArea.append("Network Error: " + error)
        }
    }
    
    Connections {
        target: latencyChecker
        
        function onLatencyResult(ip, latency) {
            logger.logLatencyResult(ip, latency)
        }
        
        function onCheckingFinished(results) {
            logArea.append(`Latency checking completed. Processed ${results.length} IPs.`)
            logger.endSession()
        }
    }
    
    Connections {
        target: logger
        
        function onLogMessageAdded(message) {
            logArea.append(message)
        }
    }
    
    // Connections for location service
    Connections {
        target: locationService
        
        function onCurrentLocationChanged() {
            // 无论复选框状态如何，都更新位置信息到TextField
            locationField.text = locationService.currentLocation
            configManager.location = locationService.currentLocation
            
            // 如果自动定位未选中，显示提示信息
            if (!autoLocationCheck.checked) {
                logger.logMessage("Location detected: " + locationService.currentLocation + " (Auto location is disabled)")
            }
        }
        
        function onLocationUpdateFailed(error) {
            logArea.append("Location update failed: " + error)
            // 失败时也无需检查复选框状态，直接显示错误信息
            locationField.text = "Location detection failed"
        }
    }
}
