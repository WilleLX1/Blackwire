#include "blackwire/ui/login_widget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace blackwire {

LoginWidget::LoginWidget(QWidget* parent) : QWidget(parent) {
    setStyleSheet("background: #313338;");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setAlignment(Qt::AlignCenter);

    auto* card = new QWidget(this);
    card->setObjectName("loginCard");
    card->setFixedWidth(480);
    card->setMinimumHeight(400);

    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(16);

    auto* title = new QLabel("Welcome back!", card);
    title->setObjectName("loginTitle");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* subtitle = new QLabel(
        "We're so excited to see you again!",
        card);
    subtitle->setObjectName("loginSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);
    layout->addSpacing(8);

    auto* server_label = new QLabel("HOME SERVER URL", card);
    server_label->setObjectName("loginFieldLabel");
    layout->addWidget(server_label);
    base_url_input_ = new QLineEdit("http://localhost:8000", card);
    base_url_input_->setPlaceholderText("http://localhost:8000 or http://yourserver.onion");
    layout->addWidget(base_url_input_);

    auto* user_label = new QLabel("USERNAME", card);
    user_label->setObjectName("loginFieldLabel");
    layout->addWidget(user_label);
    username_input_ = new QLineEdit(card);
    layout->addWidget(username_input_);

    auto* pass_label = new QLabel("PASSWORD", card);
    pass_label->setObjectName("loginFieldLabel");
    layout->addWidget(pass_label);
    password_input_ = new QLineEdit(card);
    password_input_->setEchoMode(QLineEdit::Password);
    layout->addWidget(password_input_);

    layout->addSpacing(4);

    login_button_ = new QPushButton("Log In", card);
    login_button_->setObjectName("primaryButton");
    login_button_->setFixedHeight(44);
    login_button_->setStyleSheet(
        "QPushButton { background: #5865f2; border: none; border-radius: 3px;"
        " color: #ffffff; font-size: 16px; font-weight: 500; }"
        "QPushButton:hover { background: #4752c4; }");
    layout->addWidget(login_button_);

    auto* register_row = new QHBoxLayout();
    register_row->setSpacing(4);
    auto* need_label = new QLabel("Need an account?", card);
    need_label->setObjectName("loginSubtitle");
    register_button_ = new QPushButton("Register", card);
    register_button_->setStyleSheet(
        "QPushButton { background: transparent; border: none; color: #00a8fc;"
        " font-size: 14px; font-weight: 500; padding: 0; }"
        "QPushButton:hover { text-decoration: underline; }");
    register_row->addStretch(1);
    register_row->addWidget(need_label);
    register_row->addWidget(register_button_);
    register_row->addStretch(1);
    layout->addLayout(register_row);

    layout->addStretch(1);

    outer->addWidget(card);

    connect(login_button_, &QPushButton::clicked, this, &LoginWidget::LoginRequested);
    connect(register_button_, &QPushButton::clicked, this, &LoginWidget::RegisterRequested);
}

void LoginWidget::SetBaseUrl(const QString& base_url) { base_url_input_->setText(base_url); }

QString LoginWidget::BaseUrl() const { return base_url_input_->text().trimmed(); }
QString LoginWidget::Username() const { return username_input_->text().trimmed(); }
QString LoginWidget::Password() const { return password_input_->text(); }

}  // namespace blackwire
