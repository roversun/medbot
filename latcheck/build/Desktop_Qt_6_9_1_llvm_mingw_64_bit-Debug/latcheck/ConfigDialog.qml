import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LatCheck 1.0

Dialog {
    id: configDialog
    title: "Configuration"
    width: 500
    height: 600
    modal: true
    
    // 改进虚拟键盘处理
    property real originalY: (parent ? (parent.height - height) / 2 : 0)
    
    Component.onCompleted: {
        y = originalY
    }
    
    Connections {
        target: Qt.inputMethod
        function onVisibleChanged() {
            if (Qt.inputMethod.visible) {
                // 键盘显示时，将对话框移到屏幕上方
                var keyboardHeight = Qt.inputMethod.keyboardRectangle.height
                var availableHeight = parent.height - keyboardHeight
                configDialog.y = Math.max(10, (availableHeight - configDialog.height) / 2)
            } else {
                // 键盘隐藏时，恢复居中位置
                configDialog.y = originalY
            }
        }
    }
    
    property alias serverIp: serverIpField.text
    property alias serverPort: serverPortField.value
    property alias threadCount: threadCountField.value
    property alias username: usernameField.text
    property alias clientCertPath: certPathField.text
    property alias clientKeyPath: keyPathField.text
    
    signal loginRequested(string username, string password)
    signal configSaved()
    
    ScrollView {
        anchors.fill: parent
        anchors.margins: 20
        
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
            
            GroupBox {
                title: "Client Certificates"
                Layout.fillWidth: true
                
                GridLayout {
                    columns: 3
                    anchors.fill: parent
                    
                    Label { text: "Certificate File:" }
                    TextField {
                        id: certPathField
                        Layout.fillWidth: true
                        text: configManager.clientCertPath
                        readOnly: true
                    }
                    Button {
                        text: "Browse"
                        onClicked: certFileDialog.open()
                    }
                    
                    Label { text: "Private Key File:" }
                    TextField {
                        id: keyPathField
                        Layout.fillWidth: true
                        text: configManager.clientKeyPath
                        readOnly: true
                    }
                    Button {
                        text: "Browse"
                        onClicked: keyFileDialog.open()
                    }
                }
            }
            
            RowLayout {
                Layout.fillWidth: true
                
                Button {
                    text: "Test Connection"
                    enabled: serverIpField.text.length > 0 && 
                            certPathField.text.length > 0 && 
                            keyPathField.text.length > 0
                    onClicked: {
                        networkManager.connectToServer(
                            serverIpField.text, 
                            serverPortField.value,
                            certPathField.text,
                            keyPathField.text
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
                            loginRequested(usernameField.text, passwordField.text)
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
                        configManager.clientCertPath = certPathField.text
                        configManager.clientKeyPath = keyPathField.text
                        
                        if (passwordField.text.length > 0) {
                            configManager.setPassword(passwordField.text)
                        }
                        
                        configManager.saveConfig()
                        configSaved()
                        close()
                    }
                }
                
                Button {
                    text: "Cancel"
                    onClicked: close()
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
    
    FileDialog {
        id: certFileDialog
        title: "Select Certificate File"
        nameFilters: ["PEM files (*.pem)", "All files (*)"]
        onAccepted: {
            certPathField.text = selectedFile.toString().replace("file:///", "")
        }
    }
    
    FileDialog {
        id: keyFileDialog
        title: "Select Private Key File"
        nameFilters: ["PEM files (*.pem)", "All files (*)"]
        onAccepted: {
            keyPathField.text = selectedFile.toString().replace("file:///", "")
        }
    }
}