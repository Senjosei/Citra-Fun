// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <future>
#include <QColor>
#include <QImage>
#include <QList>
#include <QLocale>
#include <QMetaType>
#include <QTime>
#include <QtConcurrent/QtConcurrentRun>
#include "citra_qt/game_list_p.h"
#include "citra_qt/main.h"
#include "citra_qt/multiplayer/host_room.h"
#include "citra_qt/multiplayer/message.h"
#include "citra_qt/multiplayer/validation.h"
#include "citra_qt/ui_settings.h"
#include "common/logging/log.h"
#include "core/announce_multiplayer_session.h"
#include "core/settings.h"
#include "ui_chat_room.h"
#include "ui_host_room.h"

HostRoomWindow::HostRoomWindow(QWidget* parent, QStandardItemModel* list,
                               std::shared_ptr<Core::AnnounceMultiplayerSession> session)
    : QDialog(parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowSystemMenuHint),
      ui(new Ui::HostRoom), announce_multiplayer_session(session), game_list(list) {
    ui->setupUi(this);

    // set up validation for all of the fields
    ui->room_name->setValidator(Validation::room_name);
    ui->username->setValidator(Validation::nickname);
    ui->port->setValidator(Validation::port);
    ui->port->setPlaceholderText(QString::number(Network::DefaultRoomPort));

    // Create a proxy to the game list to display the list of preferred games
    proxy = new ComboBoxProxyModel;
    proxy->setSourceModel(game_list);
    proxy->sort(0, Qt::AscendingOrder);
    ui->game_list->setModel(proxy);

    // Connect all the widgets to the appropriate events
    connect(ui->host, &QPushButton::pressed, this, &HostRoomWindow::Host);

    // Restore the settings:
    ui->username->setText(UISettings::values.room_nickname);
    ui->room_name->setText(UISettings::values.room_name);
    ui->port->setText(UISettings::values.room_port);
    ui->max_player->setValue(UISettings::values.max_player);
    int index = ui->host_type->findData(UISettings::values.host_type);
    if (index != -1) {
        ui->host_type->setCurrentIndex(index);
    }
    index = ui->game_list->findData(UISettings::values.game_id, GameListItemPath::ProgramIdRole);
    if (index != -1) {
        ui->game_list->setCurrentIndex(index);
    }
}

void HostRoomWindow::Host() {
    if (!ui->username->hasAcceptableInput()) {
        NetworkMessage::ShowError(NetworkMessage::USERNAME_NOT_VALID);
        return;
    }
    if (!ui->room_name->hasAcceptableInput()) {
        NetworkMessage::ShowError(NetworkMessage::ROOMNAME_NOT_VALID);
        return;
    }
    if (!ui->port->hasAcceptableInput()) {
        NetworkMessage::ShowError(NetworkMessage::PORT_NOT_VALID);
        return;
    }
    if (auto member = Network::GetRoomMember().lock()) {
        if (member->IsConnected()) {
            if (!NetworkMessage::WarnDisconnect()) {
                close();
                return;
            } else {
                member->Leave();
            }
        }
        ui->host->setDisabled(true);

        auto game_name = ui->game_list->currentData(Qt::DisplayRole).toString();
        auto game_id = ui->game_list->currentData(GameListItemPath::ProgramIdRole).toLongLong();
        auto port = ui->port->isModified() ? ui->port->text().toInt() : Network::DefaultRoomPort;
        auto password = ui->password->text().toStdString();
        if (auto room = Network::GetRoom().lock()) {
            bool created = room->Create(ui->room_name->text().toStdString(), "", port, password,
                                        ui->max_player->value(), game_name.toStdString(), game_id);
            if (!created) {
                NetworkMessage::ShowError(NetworkMessage::COULD_NOT_CREATE_ROOM);
                LOG_ERROR(Network, "Could not create room!");
                ui->host->setEnabled(true);
                return;
            }
        }
        member->Join(ui->username->text().toStdString(), "127.0.0.1", port, 0,
                     Network::NoPreferredMac, password);

        // Store settings
        UISettings::values.room_nickname = ui->username->text();
        UISettings::values.room_name = ui->room_name->text();
        UISettings::values.game_id =
            ui->game_list->currentData(GameListItemPath::ProgramIdRole).toLongLong();
        UISettings::values.max_player = ui->max_player->value();

        UISettings::values.host_type = ui->host_type->currentText();
        UISettings::values.room_port = (ui->port->isModified() && !ui->port->text().isEmpty())
                                           ? ui->port->text()
                                           : QString::number(Network::DefaultRoomPort);
        Settings::Apply();
        OnConnection();
    }
}

void HostRoomWindow::OnConnection() {
    ui->host->setEnabled(true);
    if (auto room_member = Network::GetRoomMember().lock()) {
        switch (room_member->GetState()) {
        case Network::RoomMember::State::CouldNotConnect:
            ShowError(NetworkMessage::UNABLE_TO_CONNECT);
            break;
        case Network::RoomMember::State::NameCollision:
            ShowError(NetworkMessage::USERNAME_IN_USE);
            break;
        case Network::RoomMember::State::Error:
            ShowError(NetworkMessage::UNABLE_TO_CONNECT);
            break;
        case Network::RoomMember::State::Joining:
            if (ui->host_type->currentIndex() == 0) {
                if (auto session = announce_multiplayer_session.lock()) {
                    session->Start();
                } else {
                    LOG_ERROR(Network, "Starting announce session failed");
                }
            }
            auto parent = static_cast<GMainWindow*>(parentWidget());
            parent->ChangeRoomState();
            parent->OnOpenNetworkRoom();
            close();
            break;
        }
    }
}

QVariant ComboBoxProxyModel::data(const QModelIndex& idx, int role) const {
    if (role != Qt::DisplayRole) {
        auto val = QSortFilterProxyModel::data(idx, role);
        // If its the icon, shrink it to 16x16
        if (role == Qt::DecorationRole)
            val = val.value<QImage>().scaled(16, 16, Qt::KeepAspectRatio);
        return val;
    }
    std::string filename;
    Common::SplitPath(
        QSortFilterProxyModel::data(idx, GameListItemPath::FullPathRole).toString().toStdString(),
        nullptr, &filename, nullptr);
    QString title = QSortFilterProxyModel::data(idx, GameListItemPath::TitleRole).toString();
    return title.isEmpty() ? QString::fromStdString(filename) : title;
}

bool ComboBoxProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    // TODO(jroweboy): Sort by game title not filename
    auto leftData = left.data(Qt::DisplayRole).toString();
    auto rightData = right.data(Qt::DisplayRole).toString();
    return leftData.compare(rightData) < 0;
}
