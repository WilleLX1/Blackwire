#include "blackwire/ui/login_widget.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace blackwire {

LoginWidget::LoginWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(14);

    auto* title = new QLabel("Welcome back", this);
    title->setObjectName("threadTitle");
    layout->addWidget(title);

    auto* subtitle = new QLabel(
        "Connect to your home server URL (IP/domain; .onion optional). "
        "Onion login may require a local Tor SOCKS proxy.",
        this);
    subtitle->setObjectName("conversationSubtitle");
    layout->addWidget(subtitle);

    auto* form = new QFormLayout();
    form->setSpacing(10);
    base_url_input_ = new QLineEdit("http://localhost:8000", this);
    base_url_input_->setPlaceholderText("http://localhost:8000 or http://yourserver.onion");
    username_input_ = new QLineEdit(this);
    password_input_ = new QLineEdit(this);
    password_input_->setEchoMode(QLineEdit::Password);

    form->addRow("Home Server URL (IP/domain; onion optional)", base_url_input_);
    form->addRow("Username", username_input_);
    form->addRow("Password", password_input_);

    layout->addLayout(form);

    auto* actions = new QHBoxLayout();
    actions->setSpacing(10);

    login_button_ = new QPushButton("Login", this);
    login_button_->setObjectName("primaryButton");

    register_button_ = new QPushButton("Register", this);
    register_button_->setObjectName("secondaryButton");

    actions->addWidget(login_button_);
    actions->addWidget(register_button_);
    actions->addStretch(1);
    layout->addLayout(actions);
    layout->addStretch(1);

    connect(login_button_, &QPushButton::clicked, this, &LoginWidget::LoginRequested);
    connect(register_button_, &QPushButton::clicked, this, &LoginWidget::RegisterRequested);
}

void LoginWidget::SetBaseUrl(const QString& base_url) { base_url_input_->setText(base_url); }

QString LoginWidget::BaseUrl() const { return base_url_input_->text().trimmed(); }
QString LoginWidget::Username() const { return username_input_->text().trimmed(); }
QString LoginWidget::Password() const { return password_input_->text(); }

}  // namespace blackwire
