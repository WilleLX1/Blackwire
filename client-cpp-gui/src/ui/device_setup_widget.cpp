#include "blackwire/ui/device_setup_widget.hpp"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace blackwire {

DeviceSetupWidget::DeviceSetupWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(14);

    auto* title = new QLabel("Initialize this device", this);
    title->setObjectName("threadTitle");
    layout->addWidget(title);

    auto* subtitle = new QLabel("Create and register encryption keys for this endpoint.", this);
    subtitle->setObjectName("conversationSubtitle");
    layout->addWidget(subtitle);

    auto* form = new QFormLayout();
    form->setSpacing(10);

    device_label_input_ = new QLineEdit("primary-device", this);
    form->addRow("Device Label", device_label_input_);
    layout->addLayout(form);

    setup_button_ = new QPushButton("Initialize Device", this);
    setup_button_->setObjectName("primaryButton");
    layout->addWidget(setup_button_);
    layout->addStretch(1);

    connect(setup_button_, &QPushButton::clicked, this, &DeviceSetupWidget::DeviceSetupRequested);
}

QString DeviceSetupWidget::DeviceLabel() const { return device_label_input_->text().trimmed(); }

}  // namespace blackwire
