import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.VirtualKeyboard
import LatCheck 2.0

ApplicationWindow {
    id: window
    width: 1000
    height: 700
    visible: true
    title: "LatCheck - Network Latency Checker"
    
    // 添加一个属性来存储日志区域的引用
    property var logTextAreaRef: null
    
    // 删除这行错误的alias定义
    // property alias mainLogArea: logTextArea
    
    property string testConnectionStatus: ""
    property bool testConnectionSuccess: false
    property string saveConfigStatus: ""
    property bool saveConfigSuccess: false
    
    // 统一状态显示属性
    property string statusMessage: ""
    property bool statusSuccess: false

    // 统一状态清除定时器
    Timer {
        id: statusTimer
        interval: 3000
        onTriggered: {
            statusMessage = ""
            testConnectionStatus = ""
            saveConfigStatus = ""
        }
    }
    property bool isRunning: latencyChecker.running
    property bool isLoggedIn: false
    // 删除这四行重复的属性定义：
    // property string testConnectionStatus: ""
    // property bool testConnectionSuccess: false
    // property string saveConfigStatus: ""
    // property bool saveConfigSuccess: false
    
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            
            Label {
                text: "LatCheck v2.0"
                font.bold: true
                Layout.fillWidth: true
            }
            
            ToolButton {
                text: stackView.depth === 1 ? "⚙️" : "🏠"
                font.pixelSize: 20
                onClicked: {
                    if (stackView.depth === 1) {
                        stackView.push(configPage)
                    } else {
                        stackView.pop()
                    }
                }
            }
        }
    }
    // Remove this invalid line:
    // property alias logArea: mainPage.logArea  // Remove this line
    
    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: mainPage
        
        // 主界面页面
        Component {
            id: mainPage
            
            ColumnLayout {
                property alias logArea: logTextArea  // Keep this alias in ColumnLayout
                width: parent.width
                height: parent.height
                anchors.margins: 20
                spacing: 15
                
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
                            enabled: true
                            onCheckedChanged: {
                                if (checked) {
                                    logger.logMessage("Starting automatic location detection...")
                                    locationService.startLocationUpdate()
                                    
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
                
                // Authentication Section
                GroupBox {
                    title: "Authentication"
                    Layout.fillWidth: true
                    visible: !isLoggedIn
                    
                    RowLayout {
                        anchors.fill: parent
                        
                        Label { text: "Username:" }
                        TextField {
                            id: mainUsernameField
                            Layout.fillWidth: true
                            placeholderText: "Username"
                            text: configManager.username
                            onTextChanged: configManager.username = text
                        }
                        
                        Label { text: "Password:" }
                        TextField {
                            id: mainPasswordField
                            Layout.fillWidth: true
                            placeholderText: "Password"
                            echoMode: TextInput.Password
                        }
                        
                        Button {
                            text: "Login"
                            enabled: 
                                    mainUsernameField.text.length > 0 && 
                                    mainPasswordField.text.length > 0
                            onClicked: {
                                logger.logMessage("Login attempt with username: " + mainUsernameField.text)
                                configManager.username = mainUsernameField.text
                                if (configManager.setPassword(mainPasswordField.text)) {
                                    configManager.saveConfig()
                                    networkManager.login(mainUsernameField.text, mainPasswordField.text)
                                }
                            }
                        }
                    }
                }
                
                // Control Buttons
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    Layout.bottomMargin: 10
                    
                    Item { Layout.fillWidth: true }
                    
                    Button {
                        id: startButton
                        text: "Start"
                        enabled: !isRunning && networkManager.connected && isLoggedIn
                        Layout.preferredWidth: 100
                        onClicked: {
                            if (locationField.text.length > 0) {
                                // 如果位置发生变化，记录新的位置信息
                                if (locationField.text !== configManager.location) {
                                    logger.logMessage("Location changed to: " + locationField.text)
                                    configManager.location = locationField.text
                                }
                                
                                logger.logMessage("=== LATENCY CHECK START ===")
                                logger.logMessage("Target location: " + locationField.text)
                                logger.logMessage("Thread count: " + configManager.threadCount)
                                logger.logMessage("→ Requesting server list from server...")
                                
                                configManager.saveConfig()
                                // 修改：使用sendListRequest()而不是requestIpList()
                                networkManager.sendListRequest()
                                isRunning = true
                            }
                        }
                    }
                    
                    Item { Layout.preferredWidth: 20 }
                    
                    Button {
                        id: stopButton
                        text: "Stop"
                        enabled: isRunning
                        Layout.preferredWidth: 100
                        onClicked: {
                            logger.logMessage("=== LATENCY CHECK STOPPED ===")
                            latencyChecker.stopChecking()
                            logger.endSession()
                        }
                    }
                    
                    Item { Layout.fillWidth: true }
                }
                
                RowLayout {
                    Layout.fillWidth: true
                    
                    Item { Layout.fillWidth: true }
                    
                    Label {
                        text: isRunning ? 
                            `Progress: ${latencyChecker.progress}/${latencyChecker.totalIps}` :
                            ""
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
                            id: logTextArea  // Change id from logArea to logTextArea
                            objectName: "logTextArea"  // 添加这行
                            readOnly: true
                            wrapMode: TextArea.Wrap
                            selectByMouse: true
                            selectByKeyboard: true
                            persistentSelection: true
                            font.family: "Consolas, Monaco, monospace"
                            
                            Component.onCompleted: {
                                window.logTextAreaRef = logTextArea
                                // 移除这行调试输出
                                // console.log("logTextArea reference set")
                            }
                            
                            text: "Please configure connection settings and click Start to begin latency checking.\n"
                        }
                    }
                }
                
                // Status Bar
                Label {
                    Layout.fillWidth: true
                    text: `Log File: ${logger.currentLogFile || "None"}`
                    font.pixelSize: 10
                    color: "gray"
                    Timer {
                        id: saveStatusTimer
                        interval: 3000
                        onTriggered: {
                            saveConfigStatus = ""
                        }
                    }
                }
                
                // 添加Connections组件到ColumnLayout内部
                Connections {
                    target: locationService
                    function onCurrentLocationChanged(location) {
                        if (autoLocationCheck.checked && location !== undefined && location !== null && location !== "Unknown" && location !== "") {
                            locationField.text = location
                            configManager.location = location
                            logger.logMessage("Location updated to: " + location)
                        } else if (location === undefined || location === null) {
                            logger.logMessage("Location detection returned invalid value")
                            autoLocationCheck.checked = false
                        }
                    }
                    
                    function onLocationUpdateFailed(error) {
                        logger.logMessage("Location detection failed: " + error)
                        autoLocationCheck.checked = false
                    }
                }
            }
        }
        
        // 配置界面页面
        Component {
            id: configPage
            
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                anchors.margins: 20
                spacing: 15
                
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    
                    ColumnLayout {
                        width: parent.width
                        spacing: 15
                        
                        GroupBox {
                            title: "Server Configuration"
                            Layout.fillWidth: true
                            
                            GridLayout {
                                columns: 2
                                width: parent.width
                                columnSpacing: 10
                                
                                Label { 
                                    text: "Server IP:"
                                    Layout.preferredWidth: 100
                                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                }
                                TextField {
                                    id: serverIpField
                                    Layout.fillWidth: true
                                    placeholderText: "127.0.0.1"
                                    text: configManager.serverIp
                                }
                                
                                Label { 
                                    text: "Server Port:"
                                    Layout.preferredWidth: 100
                                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                }
                                TextField {
                                    id: serverPortField
                                    Layout.fillWidth: true
                                    placeholderText: "8080"
                                    text: configManager.serverPort.toString()
                                    validator: IntValidator { bottom: 1; top: 65535 }
                                    inputMethodHints: Qt.ImhDigitsOnly
                                }
                            }
                        }
                        
                        GroupBox {
                            title: "Threading Configuration"
                            Layout.fillWidth: true
                            
                            GridLayout {
                                columns: 2
                                width: parent.width
                                columnSpacing: 10
                                
                                Label { 
                                    text: "Thread Count:"
                                    Layout.preferredWidth: 100
                                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                }
                                TextField {
                                    id: threadCountField
                                    Layout.fillWidth: true
                                    placeholderText: "50"
                                    text: configManager.threadCount.toString()
                                    validator: IntValidator { bottom: 1; top: 200 }
                                    inputMethodHints: Qt.ImhDigitsOnly
                                }
                            }
                        }
                        
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 15
                            Layout.bottomMargin: 10
                            
                            Item { Layout.fillWidth: true }
                            
                            Button {
                                text: "Test Connection"
                                enabled: serverIpField.text.length > 0
                                Layout.preferredWidth: 120
                                onClicked: {
                                    statusMessage = ""
                                    testConnectionStatus = ""
                                    networkManager.testConnection(
                                        serverIpField.text, 
                                        parseInt(serverPortField.text) || 8080,
                                        "",
                                        "",
                                        true
                                    )
                                }
                            }
                            
                            Item { Layout.preferredWidth: 20 }
                            
                            Button {
                                text: "Save"
                                Layout.preferredWidth: 100
                                onClicked: {
                                    // Clear previous status
                                    statusMessage = ""
                                    testConnectionStatus = ""
                                    saveConfigStatus = ""
                                    
                                    // Save configuration using property assignment
                                    configManager.serverIp = serverIpField.text
                                    configManager.serverPort = parseInt(serverPortField.text) || 8080
                                    configManager.threadCount = parseInt(threadCountField.text) || 50
                                    
                                    configManager.saveConfig()
                                    logger.logMessage("Configuration saved successfully.")
                                    
                                    // Show success status - 只设置统一状态
                                    statusMessage = "Configurations are saved!"
                                    statusSuccess = true
                                    
                                    // Clear status after 3 seconds
                                    statusTimer.restart()
                                }
                            }
                            
                            Item { Layout.fillWidth: true }
                        }
                        
                        // 统一状态显示标签
                        Label {
                            text: statusMessage || testConnectionStatus || saveConfigStatus
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            color: (statusSuccess || testConnectionSuccess || saveConfigSuccess) ? "green" : "red"
                            visible: (statusMessage.length > 0 || testConnectionStatus.length > 0 || saveConfigStatus.length > 0)
                            font.bold: true
                        }                        
                    }
                }
            }
        }
    }
    
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
                    duration: 300
                    easing.type: Easing.InOutQuad
                }
            }
        }
    }
    
    property bool isLoggingIn: false  // Add this to the main ApplicationWindow
    
    // Keep the Connections block but move it to the proper location within the main ApplicationWindow
    Connections {
        target: networkManager
        function onLoginResult(success, message) {
            isLoggingIn = false
            if (success) {
                isLoggedIn = true
                //logger.logMessage("Login successful: " + message)
            } else {
                isLoggedIn = false
                //logger.logMessage("Login failed: " + message)
                // Clear password field on login failure
                if (typeof mainPasswordField !== 'undefined') {
                    mainPasswordField.text = ""
                }
            }
        }
        
        function onConnectedChanged() {
            if (!networkManager.connected) {
                isLoggedIn = false
                isLoggingIn = false
            }
        }
    }

    // 删除这些重复的组件（第518-620行）
    // Authentication Section - 修改登录部分
    // GroupBox {
    //     title: "Authentication"
    //     Layout.fillWidth: true
    //     visible: !isLoggedIn  // 登录成功后隐藏
    //     ...
    // }
    
    // 新增：登出部分
    // GroupBox {
    //     title: "User Session"
    //     Layout.fillWidth: true
    //     visible: isLoggedIn  // 登录成功后显示
    //     ...
    // }

    // Ensure all event handlers are properly contained in Connections blocks
    Connections {
        target: networkManager
        function onIpListReceived(ipList) {
            // 收到服务器列表后启动延迟检测
            if (stackView.currentItem && stackView.currentItem.logArea) {
                stackView.currentItem.logArea.append("✓ Data retrieval successful: Received " + ipList.length + " servers")
                stackView.currentItem.logArea.append("→ Starting latency detection...")
            }
            // 添加logger日志
            logger.logMessage("✓ Received " + ipList.length + " servers from server")
            logger.logMessage("→ Starting latency detection with " + configManager.threadCount + " threads")
            
            latencyChecker.startChecking(ipList, configManager.threadCount)
        }
    }

    Connections {
        target: networkManager
        function onErrorOccurred(error) {
            // 使用存储的引用，直接显示原始错误消息
            if (logTextAreaRef && logTextAreaRef.append) {
                logTextAreaRef.append(error)  // 直接显示error内容，不添加时间戳前缀
            }
        }
    }

    Connections {
        target: networkManager
        // 添加测试连接结果处理函数
        function onTestConnectionResult(message, success) {
            if (stackView.currentItem && stackView.currentItem.logArea) {
                var statusMsg = success ? "✓ Successfully connected to server" : "✗ Failed to connect to server: " + message
                stackView.currentItem.logArea.append(statusMsg)
            }
        }

        // 添加报告上传结果处理（需要在NetworkManager中添加相应信号）
        function onReportUploadResult(success, reportId, message) {
            if (stackView.currentItem && stackView.currentItem.logArea) {
                if (success && reportId) {
                    stackView.currentItem.logArea.append("✓ Report upload successful, Report ID: " + reportId)
                } else {
                    stackView.currentItem.logArea.append("✗ Report upload failed: " + message)
                }
            }
            
            // 添加logger日志
            if (success && reportId) {
                logger.logMessage("✓ Report upload successful, Report ID: " + reportId)
                logger.logMessage("=== LATENCY CHECK COMPLETED ===")
            } else {
                logger.logMessage("✗ Report upload failed: " + message)
            }
        }
    }

    Connections {
        target: logger
        function onLogMessageAdded(message) {
            // 添加安全检查，避免在UI未初始化时处理信号
            if (stackView && stackView.currentItem && stackView.currentItem.logArea) {
                stackView.currentItem.logArea.append(message)
            } else {
                // 如果UI未准备好，暂存消息或输出到控制台
                console.log("Logger:", message)
            }
        }
    }
    
    // 修改Component.onCompleted，添加自动连接逻辑
    Component.onCompleted: {
        Qt.callLater(function() {
            try {
                var initialLocation = configManager.location || "Application Startup"
                logger.startNewSession(initialLocation)
                logger.logMessage("LatCheck v2.0 started")
                
                // 自动连接到服务器
                if (configManager.serverIp && configManager.serverPort > 0) {
                    networkManager.connectToServer(
                        configManager.serverIp,
                        configManager.serverPort,
                        "certs/client.crt",
                        "certs/client.key",
                        true
                    )
                }
            } catch (error) {
                console.log("Startup error:", error)
            }
        })
    }

    Connections {
        target: latencyChecker
        function onLatencyResult(serverId, latency) {
            if (stackView.currentItem && stackView.currentItem.logArea) {
                if (latency >= 0) {
                    stackView.currentItem.logArea.append("→ Node ID " + serverId + " latency: " + latency + "ms")
                } else {
                    stackView.currentItem.logArea.append("✗ Node ID " + serverId + " latency detection failed")
                }
            }
        }
        
        function onCheckingFinished(results) {
            if (stackView.currentItem && stackView.currentItem.logArea) {
                stackView.currentItem.logArea.append("✓ Latency detection completed")
                stackView.currentItem.logArea.append("→ Uploading report...")
            }
            
            // 自动上传报告
            if (isLoggedIn && results.length > 0) {
                var location = locationService.getCurrentLocation()
                networkManager.sendReportRequest(location, results)
            } else {
                logger.logMessage("✗ Cannot upload report: not logged in or no results")
            }
        }
    }

}
