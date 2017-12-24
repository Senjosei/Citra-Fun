// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <future>
#include <memory>
#include <QDialog>
#include <QFutureWatcher>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include "common/announce_multiplayer_room.h"
#include "core/announce_multiplayer_session.h"
#include "network/network.h"

namespace Ui {
class Lobby;
}

class LobbyModel;
class LobbyFilterProxyModel;

/**
 * Listing of all public games pulled from services. The lobby should be simple enough for users to
 * find the game they want to play, and join it.
 */
class Lobby : public QDialog {
    Q_OBJECT

public:
    explicit Lobby(QWidget* parent, QStandardItemModel* list,
                   std::shared_ptr<Core::AnnounceMultiplayerSession> session);
    ~Lobby();

public slots:
    /**
     * Begin the process to pull the latest room list from web services. After the listing is
     * returned from web services, `LobbyRefreshed` will be signalled
     */
    void RefreshLobby();

private slots:
    /**
     * Pulls the list of rooms from network and fills out the lobby model with the results
     */
    void OnRefreshLobby();
    /**
     * Handler for double clicking on a room in the list. Gathers the host ip and port and attempts
     * to connect. Will also prompt for a password in case one is required.
     *
     * index - The row of the proxy model that the user wants to join.
     */
    void OnJoinRoom(const QModelIndex&);

    /**
     * Handler for connection status changes. Launches the client room window if successful or
     * displays an error
     */
    void OnConnection();

    void OnStateChanged(const Network::RoomMember::State&);

signals:
    /**
     * Signalled when the latest lobby data is retrieved.
     */
    void LobbyRefreshed();

    /**
     * Signalled when the status for room connection changes.
     */
    void Connected();

    void StateChanged(const Network::RoomMember::State&);

private:
    /**
     * Removes all entries in the Lobby before refreshing.
     */
    void ResetModel();

    /**
     * Prompts for a password. Returns an empty QString in the user either did not provide a
     * password or if the user closed the window.
     */
    const QString PasswordPrompt();

    QStandardItemModel* model;
    QStandardItemModel* game_list;
    LobbyFilterProxyModel* proxy;

    std::future<AnnounceMultiplayerRoom::RoomList> room_list_future;
    std::weak_ptr<Core::AnnounceMultiplayerSession> announce_multiplayer_session;
    std::unique_ptr<Ui::Lobby> ui;
    QFutureWatcher<void>* watcher;
};

/**
 * Proxy Model for filtering the lobby
 */
class LobbyFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT;

public:
    explicit LobbyFilterProxyModel(QWidget* parent, QStandardItemModel* list);
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    void sort(int column, Qt::SortOrder order) override;

public slots:
    void SetFilterOwned(bool);
    void SetFilterFull(bool);

private:
    QStandardItemModel* game_list;
    bool filter_owned = false;
    bool filter_full = false;
};
