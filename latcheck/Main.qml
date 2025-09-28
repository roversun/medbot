import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// import QtQuick.VirtualKeyboard
import Qt.labs.folderlistmodel 2.1
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


    header: ToolBar {
        RowLayout {
            anchors.fill: parent


            Label {
                text: "LatCheck v2.0"
                font.bold: true
                Layout.fillWidth: true
            }

            // 添加证书用户名显示
            Label {
                id: certificateSubjectName
                text: {
                    return configManager.getCertificateSubjectName()
                }
                font.bold: true
                Layout.alignment: Qt.AlignRight
                visible: certificateSubjectName.text.length > 0
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
     
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.rightMargin: 20
                        anchors.fill: parent

                        TextField {
                            id: mainUsernameField
                            placeholderText: "Username"
                            text: configManager.username
                            Layout.fillWidth: true
                            onTextChanged: {
                                configManager.username = text
                            }
                        }

                        TextField {
                            id: mainPasswordField
                            placeholderText: "Password"
                            echoMode: TextInput.Password
                            Layout.fillWidth: true
                            onTextChanged: {
                                configManager.setPassword(text)
                            }
                        }

                        Button {
                            id: loginButton
                            Layout.preferredWidth: 100
                            text: isLoggedIn ? "Logout" : "Login"
                            // 修改：根据登录状态设置不同的启用条件
                            enabled: isLoggedIn || (
                                    mainUsernameField.text.length > 0 &&
                                    mainPasswordField.text.length > 0 &&
                                    !isRunning  // 运行时禁用登录/登出按钮
                            )

                            onClicked: {
                                if (isLoggedIn) {
                                    // 登出逻辑 - 不操作任何输入框
                                    isLoggedIn = false
                                    networkManager.disconnectFromServer()
                                    logger.logMessage("User logged out")
                                } else {
                                    // 登录逻辑保持不变
                                    logger.logMessage("Login attempt with username: " + mainUsernameField.text)

                                    // 如果已连接，先断开
                                    if (networkManager.connected) {
                                        logger.logMessage("Disconnecting existing connection...")
                                        networkManager.disconnectFromServer()
                                    }

                                    configManager.username = mainUsernameField.text
                                    if (configManager.setPassword(mainPasswordField.text)) {
                                        configManager.saveConfig()

                                        // 延迟一点时间确保断开完成后再连接
                                        Qt.callLater(function() {
                                            networkManager.login(
                                                mainUsernameField.text,
                                                mainPasswordField.text
                                            )
                                        })
                                    }
                                }
                            }
                        }
                        Button {
                            // 修改：根据登录状态动态显示按钮文本
                            text: "Change Password"
                            // 添加：设置宽度与start按钮一致
                            Layout.preferredWidth: 120
                            // 修改：根据登录状态设置不同的启用条件
                            enabled: !isLoggingIn && !isRunning  // 运行时禁用修改密码按钮
                            onClicked: {
                                // changePasswordDialog.show()
                                changePasswordDialog.visible = true

                            }
                        }
                    }
                }

                // Control Buttons
                // 修改控制按钮区域的RowLayout，添加右侧margin并移除填充空间
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    Layout.bottomMargin: 0
                    Layout.rightMargin: 10
                
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
                
                        // 移除：多余的logout按钮，已在认证区域实现
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

                                logger.logMessage("Latency check is starting...")
                                logger.logMessage("Target location: " + locationField.text)
                                logger.logMessage("Thread count: " + configManager.threadCount)
                                logger.logMessage("Requesting server list from server...")

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
                
                    // 修改：移除填充空间，使用右侧margin来控制对齐
                    Item { Layout.preferredWidth: 10 }
                
                    // 添加：Export IP按钮
                    Button {
                        id: exportIpButton
                        text: "Save IPs"
                        // 只有在有IP列表时才启用
                        enabled: isLoggedIn && receivedIpList.length > 0 && !isRunning
                        Layout.preferredWidth: 100
                        // 修改exportIpButton的onClicked处理
                        onClicked:  {
                            // 直接使用用户提供的默认路径或请求用户输入
                            var defaultPath = "ip_list.txt";
                            // 在Windows上使用当前目录下的ip_list.txt
                            var filePath = defaultPath;

                            // 保存IP列表
                            if (networkManager.saveIpListToFile()) {
                                // statusMessage = "IP列表已成功导出到: " + filePath;
                                // statusSuccess = true;
                                // logger.logMessage( "✅ IP list has been saved to " + filePath);
                            } else {
                                // statusMessage = "导出IP列表失败";
                                // statusSuccess = false;
                                logger.logMessage( "❌ IP list failed saving to " + filePath);
                            }
                            // statusTimer.restart();
                        }
                    }
                
                    Item { Layout.preferredWidth: 10 }
                
                    // 修改：Clear Log按钮 - 使用右侧margin来控制对齐
                    Button {
                        id: clearLogButton
                        text: "Clear Log"
                        enabled: !isRunning
                        Layout.preferredWidth: 100  // 与Change Password按钮宽度一致
                        onClicked: {
                            logTextArea.clearLog()
                            logger.logMessage("Log cleared")
                        }
                    }
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
                                append(message)
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
                                        false
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
    // InputPanel {
    //     id: inputPanel
    //     z: 99
    //     x: 0
    //     y: window.height
    //     width: window.width

    //     // 使用缩放变换
    //     // transform: Scale {
    //     //     xScale: 1.0
    //     //     yScale: 0.6  // 缩放到60%高度
    //     //     origin.x: inputPanel.width / 2
    //     //     origin.y: inputPanel.height+2
    //     // }
    //     states: State {
    //         name: "visible"
    //         when: inputPanel.active
    //         PropertyChanges {
    //             target: inputPanel
    //             y: window.height - inputPanel.height
    //         }
    //     }
    //     transitions: Transition {
    //         from: ""
    //         to: "visible"
    //         reversible: true
    //         ParallelAnimation {
    //             NumberAnimation {
    //                 properties: "y"
    //                 duration: 300
    //                 easing.type: Easing.InOutQuad
    //             }
    //         }
    //     }
    // }

    property bool isLoggingIn: false  // Add this to the main ApplicationWindow
    // 添加：存储接收到的IP列表
    property variant receivedIpList: []

    // Keep the Connections block but move it to the proper location within the main ApplicationWindow
    Connections {
        target: networkManager
        function onConnectedChanged() {
            if (!networkManager.connected) {
                isLoggedIn = false
                isLoggingIn = false  // 合并：添加isLoggingIn重置
                receivedIpList = []  // 网络断开时清空IP列表
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
                //logger.logMessage("✓ Login successful: " + message)
            } else {
                isLoggedIn = false
                receivedIpList = []  // 登录失败时清空IP列表
                // 添加：登录失败时确保重置状态
                if (isRunning) {
                    latencyChecker.stopChecking()
                    isRunning = false
                }
                //logger.logMessage("✗ Login failed: " + message)
                // Clear password field on login failure
                if (typeof mainPasswordField !== 'undefined') {
                    mainPasswordField.text = ""
                }
            }
        }

    }



    // Ensure all event handlers are properly contained in Connections blocks
    Connections {
        target: networkManager
        function onIpListReceived(ipList) {
            // 保存接收到的IP列表
            receivedIpList = ipList

            // latencyChecker.startChecking(ipList, configManager.threadCount)
        }
    }

    Connections {
        target: networkManager
        function onErrorOccurred(error) {
            logger.logMessage(error);
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

        }

    }

     // 在现有 Connections 块之后添加
    Connections {
        target: networkManager
        function onLatencyCheckFinished(results) {
            // 显示延迟检测结果
            // logger.logMessage("Received latency check results for " + results.length + " servers")

            // 也可以直接在这里调用发送报告的代码（如果需要）
            if (isLoggedIn && networkManager.connected && results.length > 0) {
                var location = configManager.location
                networkManager.sendReportRequest(location, results)
            }
        }

        function onReportUploadResult(success, reportId, message) {
            // 在报告上传完成后立即重置运行状态
            isRunning = false

            // 记录上传结果信息
            if (success) {
                logger.logMessage("✅ Report uploaded successfully. ")
            } else {
                logger.logMessage("❌ Report upload failed: " + message)
            }
        }
    }
    Connections {
    target: networkManager
    function onChangePasswordResult(success, message) {
        if (success) {
            logger.logMessage("✅ Password changed successfully")            
        } else {
            logger.logMessage("❌ Password change failed, " + message)
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
        function onCheckingFinished(results) {
            // 添加：确保检测完成后重置状态
            isRunning = false

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

    Connections {
        target: logger
        function onLogMessageAdded(message) {
            if (logTextAreaRef && logTextAreaRef.append) {
                logTextAreaRef.appendLogWithLineNumber(message);
            }

        }
    }

    // Change Password对话框
    Dialog {
        id: changePasswordDialog
        title: "🔑 Change Password"
        // parent: window.overlay
        modal: true  // 设置为模态对话框
        anchors.centerIn: parent  // 放置在屏幕中央
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

        // 设置对话框尺寸
        width: 400
        height: 250

        // 自定义按钮布局，不使用standardButtons
        footer: DialogButtonBox {
            alignment: Qt.AlignRight

            Button {
                text: "Change Password"
                onClicked: {
                    if (newPasswordField.text !== confirmPasswordField.text) {
                        passwordErrorLabel.text = "New passwords do not match!"
                        return
                    }

                    if (newPasswordField.text.length < 6) {
                        passwordErrorLabel.text = "New password must be at least 6 characters long!"
                        return
                    }

                    // 处理密码修改逻辑
                    changePasswordDialog.accept()
                }
            }

            Button {
                text: "Cancel"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }

        // 对话框内容
        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Label {
                text: "Current Password:"
            }
            TextField {
                id: currentPasswordField
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: "Enter your current password"
            }

            Label {
                text: "New Password:"
            }
            TextField {
                id: newPasswordField
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: "Enter your new password"
            }

            Label {
                text: "Confirm New Password:"
            }
            TextField {
                id: confirmPasswordField
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: "Confirm your new password"
            }
            Label {
                id: passwordErrorLabel
                Layout.fillWidth: true
                text: ""
                color: "red"
                visible: text.length > 0
            }
        }

        // 处理按钮点击
        onAccepted: {
            console.log("密码修改确认")

            // 调用密码修改功能
            networkManager.changePassword(
                configManager.username,  // 当前用户名
                currentPasswordField.text,  // 当前密码
                newPasswordField.text       // 新密码
            )

            // 清空历史数据
            currentPasswordField.text = ""
            newPasswordField.text = ""
            confirmPasswordField.text = ""

        }


        onRejected: {
            console.log("密码修改取消")
            // 清空历史数据
            currentPasswordField.text = ""
            newPasswordField.text = ""
            confirmPasswordField.text = ""

        }
    }

}
