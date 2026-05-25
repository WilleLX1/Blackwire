#pragma once

#include <QWidget>

#include "blackwire/models/view_models.hpp"

class QLineEdit;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace blackwire {

class LoginWidget final : public QWidget {
    Q_OBJECT

public:
    explicit LoginWidget(QWidget* parent = nullptr);

    void SetBaseUrl(const QString& base_url);
    void ShowLoginPage();
    void ShowRegisterPage();
    void SetRegistrationServerInfoLoading();
    void SetRegistrationServerInfo(const RegistrationServerInfoView& info);
    void SetRegistrationServerInfoError(const QString& error);
    QString BaseUrl() const;
    QString Username() const;
    QString Password() const;
    QString ConfirmPassword() const;

signals:
    void LoginRequested();
    void RegisterRequested();
    void RegistrationServerInfoRefreshRequested(const QString& base_url);

private:
    void ScheduleRegistrationServerInfoRefresh();
    QString RegisterBaseUrl() const;

    QStackedWidget* page_stack_;
    QWidget* login_page_;
    QWidget* register_page_;
    QLineEdit* base_url_input_;
    QLineEdit* username_input_;
    QLineEdit* password_input_;
    QLineEdit* register_base_url_input_;
    QLineEdit* register_username_input_;
    QLineEdit* register_password_input_;
    QLineEdit* register_confirm_password_input_;
    QLabel* register_server_info_label_;
    QPushButton* login_button_;
    QPushButton* register_button_;
    QPushButton* create_account_button_;
    QPushButton* back_to_login_button_;
    QTimer* server_info_timer_;
};

}  // namespace blackwire
