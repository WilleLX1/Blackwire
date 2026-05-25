#include "blackwire/ui/login_widget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace blackwire {

namespace {

QString ShortKey(QString value) {
    value = value.trimmed();
    if (value.size() <= 24) {
        return value;
    }
    return QString("%1...%2").arg(value.left(12), value.right(8));
}

QString ShortAuthority(QString value) {
    value = value.trimmed();
    if (value.isEmpty()) {
        return "not advertised";
    }
    if (value == "local.invalid") {
        return "local.invalid (dev fallback)";
    }
    if (value.size() <= 34) {
        return value;
    }
    return QString("%1...%2").arg(value.left(10), value.right(16));
}

QLabel* FieldLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName("loginFieldLabel");
    return label;
}

}  // namespace

LoginWidget::LoginWidget(QWidget* parent) : QWidget(parent) {
    setObjectName("loginShell");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(32, 32, 32, 32);
    outer->setAlignment(Qt::AlignCenter);

    page_stack_ = new QStackedWidget(this);
    page_stack_->setObjectName("loginPageStack");
    outer->addWidget(page_stack_, 0, Qt::AlignCenter);

    login_page_ = new QWidget(page_stack_);
    auto* login_card = new QWidget(login_page_);
    login_card->setObjectName("loginCard");
    login_card->setFixedWidth(440);
    login_card->setMinimumHeight(520);

    auto* login_outer = new QVBoxLayout(login_page_);
    login_outer->setContentsMargins(0, 0, 0, 0);
    login_outer->addWidget(login_card);

    auto* layout = new QVBoxLayout(login_card);
    layout->setContentsMargins(34, 34, 34, 30);
    layout->setSpacing(14);

    auto* logo = new QLabel("B", login_card);
    logo->setObjectName("loginLogo");
    logo->setAlignment(Qt::AlignCenter);
    logo->setFixedSize(64, 64);
    layout->addWidget(logo, 0, Qt::AlignHCenter);

    auto* title = new QLabel("Welcome to Blackwire", login_card);
    title->setObjectName("loginTitle");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* subtitle = new QLabel("Encrypted DMs, groups, and calls on your home server.", login_card);
    subtitle->setObjectName("loginSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);
    layout->addSpacing(8);

    layout->addWidget(FieldLabel("HOME SERVER URL", login_card));
    base_url_input_ = new QLineEdit("http://localhost:8000", login_card);
    base_url_input_->setPlaceholderText("https:// or http:// (TLS optional)");
    layout->addWidget(base_url_input_);

    layout->addWidget(FieldLabel("USERNAME", login_card));
    username_input_ = new QLineEdit(login_card);
    username_input_->setPlaceholderText("alice");
    layout->addWidget(username_input_);

    layout->addWidget(FieldLabel("PASSWORD", login_card));
    password_input_ = new QLineEdit(login_card);
    password_input_->setPlaceholderText("Password123!");
    password_input_->setEchoMode(QLineEdit::Password);
    layout->addWidget(password_input_);

    auto* password_hint = new QLabel("Use at least 8 characters with upper, lower, digit, and symbol.", login_card);
    password_hint->setObjectName("loginHint");
    password_hint->setWordWrap(true);
    layout->addWidget(password_hint);

    layout->addSpacing(4);

    login_button_ = new QPushButton("Log In", login_card);
    login_button_->setObjectName("primaryButton");
    login_button_->setFixedHeight(44);
    layout->addWidget(login_button_);

    auto* register_row = new QHBoxLayout();
    register_row->setSpacing(4);
    auto* need_label = new QLabel("Need an account?", login_card);
    need_label->setObjectName("loginSubtitle");
    register_button_ = new QPushButton("Register", login_card);
    register_button_->setObjectName("linkButton");
    register_row->addStretch(1);
    register_row->addWidget(need_label);
    register_row->addWidget(register_button_);
    register_row->addStretch(1);
    layout->addLayout(register_row);

    layout->addStretch(1);
    page_stack_->addWidget(login_page_);

    register_page_ = new QWidget(page_stack_);
    auto* register_card = new QWidget(register_page_);
    register_card->setObjectName("loginCard");
    register_card->setFixedWidth(500);
    register_card->setMinimumHeight(640);

    auto* register_outer = new QVBoxLayout(register_page_);
    register_outer->setContentsMargins(0, 0, 0, 0);
    register_outer->addWidget(register_card);

    auto* register_layout = new QVBoxLayout(register_card);
    register_layout->setContentsMargins(34, 30, 34, 28);
    register_layout->setSpacing(12);

    auto* register_title = new QLabel("Create account", register_card);
    register_title->setObjectName("loginTitle");
    register_title->setAlignment(Qt::AlignCenter);
    register_layout->addWidget(register_title);

    auto* register_subtitle = new QLabel("Check the home server identity before registering.", register_card);
    register_subtitle->setObjectName("loginSubtitle");
    register_subtitle->setAlignment(Qt::AlignCenter);
    register_subtitle->setWordWrap(true);
    register_layout->addWidget(register_subtitle);

    register_layout->addWidget(FieldLabel("HOME SERVER URL", register_card));
    register_base_url_input_ = new QLineEdit("http://localhost:8000", register_card);
    register_base_url_input_->setPlaceholderText("https:// or http:// (TLS optional)");
    register_layout->addWidget(register_base_url_input_);

    register_server_info_label_ = new QLabel("Server information will appear here.", register_card);
    register_server_info_label_->setObjectName("serverInfoPanel");
    register_server_info_label_->setWordWrap(true);
    register_server_info_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    register_layout->addWidget(register_server_info_label_);

    register_layout->addWidget(FieldLabel("USERNAME", register_card));
    register_username_input_ = new QLineEdit(register_card);
    register_username_input_->setPlaceholderText("alice");
    register_layout->addWidget(register_username_input_);

    register_layout->addWidget(FieldLabel("PASSWORD", register_card));
    register_password_input_ = new QLineEdit(register_card);
    register_password_input_->setPlaceholderText("Password123!");
    register_password_input_->setEchoMode(QLineEdit::Password);
    register_layout->addWidget(register_password_input_);

    register_layout->addWidget(FieldLabel("CONFIRM PASSWORD", register_card));
    register_confirm_password_input_ = new QLineEdit(register_card);
    register_confirm_password_input_->setEchoMode(QLineEdit::Password);
    register_layout->addWidget(register_confirm_password_input_);

    auto* register_hint = new QLabel("Use uppercase, lowercase, a digit, and a special character.", register_card);
    register_hint->setObjectName("loginHint");
    register_hint->setWordWrap(true);
    register_layout->addWidget(register_hint);

    create_account_button_ = new QPushButton("Create Account", register_card);
    create_account_button_->setObjectName("primaryButton");
    create_account_button_->setFixedHeight(44);
    register_layout->addWidget(create_account_button_);

    back_to_login_button_ = new QPushButton("Back to login", register_card);
    back_to_login_button_->setObjectName("linkButton");
    register_layout->addWidget(back_to_login_button_, 0, Qt::AlignHCenter);

    page_stack_->addWidget(register_page_);

    server_info_timer_ = new QTimer(this);
    server_info_timer_->setSingleShot(true);
    server_info_timer_->setInterval(500);

    connect(login_button_, &QPushButton::clicked, this, &LoginWidget::LoginRequested);
    connect(register_button_, &QPushButton::clicked, this, &LoginWidget::ShowRegisterPage);
    connect(create_account_button_, &QPushButton::clicked, this, &LoginWidget::RegisterRequested);
    connect(back_to_login_button_, &QPushButton::clicked, this, &LoginWidget::ShowLoginPage);
    connect(register_base_url_input_, &QLineEdit::textChanged, this, [this]() {
        if (page_stack_->currentWidget() == register_page_) {
            ScheduleRegistrationServerInfoRefresh();
        }
    });
    connect(server_info_timer_, &QTimer::timeout, this, [this]() {
        emit RegistrationServerInfoRefreshRequested(RegisterBaseUrl());
    });
}

void LoginWidget::SetBaseUrl(const QString& base_url) {
    base_url_input_->setText(base_url);
    register_base_url_input_->setText(base_url);
}

void LoginWidget::ShowLoginPage() {
    server_info_timer_->stop();
    base_url_input_->setText(RegisterBaseUrl());
    page_stack_->setCurrentWidget(login_page_);
}

void LoginWidget::ShowRegisterPage() {
    register_base_url_input_->setText(BaseUrl());
    register_username_input_->setText(username_input_->text().trimmed());
    register_password_input_->setText(password_input_->text());
    register_confirm_password_input_->clear();
    page_stack_->setCurrentWidget(register_page_);
    ScheduleRegistrationServerInfoRefresh();
}

void LoginWidget::SetRegistrationServerInfoLoading() {
    register_server_info_label_->setProperty("state", "loading");
    register_server_info_label_->setText("Checking federation identity...");
    register_server_info_label_->style()->unpolish(register_server_info_label_);
    register_server_info_label_->style()->polish(register_server_info_label_);
}

void LoginWidget::SetRegistrationServerInfo(const RegistrationServerInfoView& info) {
    register_server_info_label_->setProperty("state", "ok");
    const QString onion_display = ShortAuthority(info.server_onion);
    register_server_info_label_->setText(
        QString("Server: %1\nFederation protocol: v%2\nServer identity: %3\nIdentity binding: %4\nSigning key: %5\nMessages: %6\nCalls: %7\nAttachments: %8")
            .arg(
                info.base_url,
                info.federation_version.isEmpty() ? "?" : info.federation_version,
                onion_display,
                info.identity_binding_mode.isEmpty() ? "not advertised" : info.identity_binding_mode,
                ShortKey(info.signing_public_key),
                info.supported_message_modes,
                info.supported_call_modes,
                info.attachment_limits));
    register_server_info_label_->setToolTip(info.server_onion.trimmed());
    register_server_info_label_->style()->unpolish(register_server_info_label_);
    register_server_info_label_->style()->polish(register_server_info_label_);
}

void LoginWidget::SetRegistrationServerInfoError(const QString& error) {
    register_server_info_label_->setProperty("state", "error");
    register_server_info_label_->setText(QString("Could not read federation information.\n%1").arg(error.trimmed()));
    register_server_info_label_->style()->unpolish(register_server_info_label_);
    register_server_info_label_->style()->polish(register_server_info_label_);
}

QString LoginWidget::BaseUrl() const {
    if (page_stack_->currentWidget() == register_page_) {
        return RegisterBaseUrl();
    }
    return base_url_input_->text().trimmed();
}

QString LoginWidget::Username() const {
    if (page_stack_->currentWidget() == register_page_) {
        return register_username_input_->text().trimmed();
    }
    return username_input_->text().trimmed();
}

QString LoginWidget::Password() const {
    if (page_stack_->currentWidget() == register_page_) {
        return register_password_input_->text();
    }
    return password_input_->text();
}

QString LoginWidget::ConfirmPassword() const {
    return register_confirm_password_input_->text();
}

void LoginWidget::ScheduleRegistrationServerInfoRefresh() {
    SetRegistrationServerInfoLoading();
    server_info_timer_->start();
}

QString LoginWidget::RegisterBaseUrl() const {
    return register_base_url_input_->text().trimmed();
}

}  // namespace blackwire
