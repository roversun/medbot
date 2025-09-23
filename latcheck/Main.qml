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
                anchors.margins: 0
                spacing: 0
                
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
                            inputMethodHints: Qt.ImhNone  // 允许所有输入法
                        }
                        
                        CheckBox {
                            id: autoLocationCheck
                            text: "Auto Location"
                            enabled: true
                            onCheckedChanged: {
                                if (checked) {
                                    // logger.logMessage("Starting automatic location detection...")
                                    locationService.startLocationUpdate()
                                    
                                    if (locationService.currentLocation !== "Unknown" && locationService.currentLocation !== "") {
                                        locationField.text = locationService.currentLocation
                                        configManager.location = locationService.currentLocation
                                        logger.logMessage("Using cached location: " + locationService.currentLocation)
                                    }
                                } else {
                                    // logger.logMessage("Stopping automatic location detection...")
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
                            inputMethodHints: Qt.ImhNone  // 允许所有输入法
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
                                    mainPasswordField.text.length > 0 &&
                                    !isRunning  // 添加：运行时禁用login按钮
                            onClicked: {
                                logger.logMessage("Login attempt with username: " + mainUsernameField.text)
                                
                                // 添加：如果已连接，先断开
                                if (networkManager.connected) {
                                    logger.logMessage("→ Disconnecting existing connection...")
                                    networkManager.disconnectFromServer()
                                }
                                
                                configManager.username = mainUsernameField.text
                                if (configManager.setPassword(mainPasswordField.text)) {
                                    configManager.saveConfig()
                                    
                                    // 延迟一点时间确保断开完成后再连接
                                    Qt.callLater(function() {
                                        networkManager.connectToServer(
                                            configManager.serverIp,
                                            configManager.serverPort,
                                            "",
                                            "",
                                            true
                                        )
                                        
                                        // 连接成功后自动登录
                                        networkManager.login(mainUsernameField.text, mainPasswordField.text)
                                    })
                                }
                            }
                        }
                    }
                }
                
                // Control Buttons
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    Layout.bottomMargin: 0
                    
                    Item { Layout.fillWidth: true }
                    
                    // 添加：状态图标和文本
                    RowLayout {
                        spacing: 8
                        
                        Label {
                            id: statusIcon
                            text: {
                                if (isRunning) {
                                    return "🔄"  // 运行中图标
                                } else if (networkManager.connected && isLoggedIn) {
                                    return "✅"  // 就绪图标
                                } else {
                                    return "❌"  // 断开图标
                                }
                            }
                            font.pixelSize: 16
                        }
                        
                        Label {
                            id: statusText
                            text: {
                                if (isRunning) {
                                    return "运行中..."
                                } else if (networkManager.connected && isLoggedIn) {
                                    return "Ready"
                                } else {
                                    return "Disconnected"
                                }
                            }
                            color: {
                                if (isRunning) {
                                    return "#2196F3"  // 蓝色
                                } else if (networkManager.connected && isLoggedIn) {
                                    return "#4CAF50"  // 绿色
                                } else {
                                    return "#F44336"  // 红色
                                }
                            }
                            font.bold: true
                        }
                        
                        // 添加：logout按钮
                        Button {
                            text: "Logout"
                            visible: networkManager.connected && isLoggedIn
                            onClicked: {
                                isLoggedIn = false
                                networkManager.disconnectFromServer()
                                logger.logMessage("User logged out")
                                mainPasswordField.text = ""
                            }
                        }
                    }
                    
                    Item { Layout.preferredWidth: 20 }
                    
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
                            latencyChecker.stopChecking()
                            // 添加：立即重置运行状态
                            isRunning = false
                            logger.endSession()
                            logger.logMessage("=== LATENCY CHECK STOPPED ===")
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
                            id: logTextArea
                            objectName: "logTextArea"
                            readOnly: true
                            wrapMode: TextArea.Wrap
                            selectByMouse: true
                            selectByKeyboard: true
                            persistentSelection: true
                            font.family: "Consolas, Monaco, monospace"
                            
                            property int lineNumber: 1
                            
                            Component.onCompleted: {
                                window.logTextAreaRef = logTextArea
                            }
                            
                            // 添加带行号的日志函数
                            function appendLogWithLineNumber(message) {
                                var logLine = String(lineNumber).padStart(3, '0') + ": " + message
                                append(logLine)
                                lineNumber++
                                // 自动滚动到底部
                                Qt.callLater(function() {
                                    cursorPosition = length
                                })
                            }
                            
                            // 清空日志并重置行号
                            function clearLog() {
                                clear()
                                lineNumber = 1
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
                        // 无论Auto Location是否勾选，都输出日志
                        // if (location !== undefined && location !== null && location !== "Unknown" && location !== "") {
                        //     logger.logMessage("Location detected: " + location)
                        // }
                        
                        // 只有勾选Auto Location时才自动填充到界面
                        if (autoLocationCheck.checked && location !== undefined && location !== null && location !== "Unknown" && location !== "") {
                            locationField.text = location
                            configManager.location = location
                            // logger.logMessage("Auto location updated to: " + location)
                        } else if (location === undefined || location === null) {
                            // logger.logMessage("Location detection returned invalid value")
                            autoLocationCheck.checked = false
                        }
                    }
                    
                    function onLocationUpdateFailed(error) {
                        // logger.logMessage("Location detection failed: " + error)
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
        
        // 使用缩放变换
        // transform: Scale {
        //     xScale: 1.0
        //     yScale: 0.6  // 缩放到60%高度
        //     origin.x: inputPanel.width / 2
        //     origin.y: inputPanel.height+2
        // }
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
        function onConnectedChanged() {
            if (!networkManager.connected) {
                isLoggedIn = false
                isLoggingIn = false  // 合并：添加isLoggingIn重置
                // 添加：网络断开时重置运行状态
                if (isRunning) {
                    latencyChecker.stopChecking()
                    isRunning = false
                    // logger.logMessage("✗ Network disconnected, stopping latency check")
                }
            }
        }
        
        function onLoginResult(success, message) {
            if (success) {
                isLoggedIn = true
                logger.logMessage("✓ Login successful: " + message)
            } else {
                isLoggedIn = false
                // 添加：登录失败时确保重置状态
                if (isRunning) {
                    latencyChecker.stopChecking()
                    isRunning = false
                }
                logger.logMessage("✗ Login failed: " + message)
                // Clear password field on login failure
                if (typeof mainPasswordField !== 'undefined') {
                    mainPasswordField.text = ""
                }
            }
        }
        
        // 删除重复的onConnectedChanged函数定义
        // function onConnectedChanged() {
        //     if (!networkManager.connected) {
        //         isLoggedIn = false
        //         isLoggingIn = false
        //     }
        // }
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
            // if (stackView.currentItem && stackView.currentItem.logArea) {
            //     stackView.currentItem.logArea.append("✓ Data retrieval successful: Received " + ipList.length + " servers")
            //     stackView.currentItem.logArea.append("→ Starting latency detection...")
            // }
            // // 添加logger日志
            // logger.logMessage("✓ Received " + ipList.length + " servers from server")
            // logger.logMessage("→ Starting latency detection with " + configManager.threadCount + " threads")
            
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
            // 更新状态变量以显示在状态条中
            testConnectionStatus = message
            testConnectionSuccess = success
            
            // 启动定时器清除状态
            statusTimer.restart()
            
            // if (stackView.currentItem && stackView.currentItem.logArea) {
            //     var statusMsg = success ? "✓ Successfully connected to server" : "✗ Failed to connect to server: " + message
            //     stackView.currentItem.logArea.append(statusMsg)
            // }
        }

        // 添加报告上传结果处理（需要在NetworkManager中添加相应信号）
        // function onReportUploadResult(success, reportId, message) {
        //     if (stackView.currentItem && stackView.currentItem.logArea) {
        //         if (success && reportId) {
        //             stackView.currentItem.logArea.append("✓ Report upload successful, Report ID: " + reportId)
        //         } else {
        //             stackView.currentItem.logArea.append("✗ Report upload failed: " + message)
        //         }
        //     }
            
        //     // 添加logger日志
        //     if (success && reportId) {
        //         logger.logMessage("✓ Report upload successful, Report ID: " + reportId)
        //         logger.logMessage("=== LATENCY CHECK COMPLETED ===")
        //     } else {
        //         logger.logMessage("✗ Report upload failed: " + message)
        //     }
        // }
    }

    Connections {
        target: logger
        function onLogMessageAdded(message) {
            // 删除所有日志显示逻辑
            if (stackView && stackView.currentItem && stackView.currentItem.logArea) {
                stackView.currentItem.logArea.append(message)
            }
        }
    }
    
    // Keep only one Component.onCompleted block and combine all logic
    Component.onCompleted: {
        // Original startup logic
        Qt.callLater(function() {
            try {
                var initialLocation = configManager.location || "Application Startup"
                logger.startNewSession(initialLocation)
                logger.logMessage("LatCheck v2.0 started")
            } catch (error) {
                console.log("Startup error:", error)
            }
        })
        
        // Virtual keyboard language setting
        if (typeof VirtualKeyboardSettings !== 'undefined') {
            VirtualKeyboardSettings.locale = "en_US"
        }
    }

    Connections {
        target: latencyChecker
        // 移除延时结果的日志输出
        // function onLatencyResult(serverId, ipAddr, latency) {
        //     if (stackView.currentItem && stackView.currentItem.logTextArea) {
        //         if (latency >= 0) {
        //             stackView.currentItem.logTextArea.appendLogWithLineNumber("→ Node ID " + serverId + " latency: " + latency + "ms")
        //         } else {
        //             stackView.currentItem.logTextArea.appendLogWithLineNumber("✗ Node ID " + serverId + " latency detection failed")
        //         }
        //     }
        // }
        function onCheckingFinished(results) {
            // 添加：确保检测完成后重置状态
            isRunning = false
            
            // if (stackView.currentItem && stackView.currentItem.logArea) {
            //     stackView.currentItem.logArea.append("✓ Latency detection completed")
            //     stackView.currentItem.logArea.append("→ Uploading report...")
            // }
            
            // 自动上传报告
            if (isLoggedIn && results.length > 0) {
                var location = configManager.location // 修改为使用与日志相同的位置数据源
                networkManager.sendReportRequest(location, results)
            } else {
                // logger.logMessage("✗ Cannot upload report: not logged in or no results")
            }
        }
        
        // 添加：监听running状态变化
        function onRunningChanged() {
            // 同步latencyChecker的running状态到isRunning
            if (isRunning !== latencyChecker.running) {
                isRunning = latencyChecker.running
            }
        }
    }

}