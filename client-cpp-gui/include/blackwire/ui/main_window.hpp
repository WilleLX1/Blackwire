#pragma once

#include <QMainWindow>

namespace blackwire {

class ApplicationController;
class LoginWidget;
class DeviceSetupWidget;
class ChatWidget;
class SettingsDialog;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(ApplicationController& controller, QWidget* parent = nullptr);

private:
    ApplicationController& controller_;

    LoginWidget* login_widget_;
    DeviceSetupWidget* device_widget_;
    ChatWidget* chat_widget_;
    SettingsDialog* settings_dialog_;
    QWidget* stacked_container_;
};

}  // namespace blackwire
