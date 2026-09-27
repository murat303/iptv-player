#pragma once

namespace tsvitch {

/// Asks the server for the subscription (status, end date, connections) and shows it in a dialog
void showXtreamAccountInfo();

/// At most once a day: reminds the user when the subscription ends within a week
void checkXtreamExpiry();

/// Asks the provider three small things one after the other (account, series categories, the last opened
/// series) and shows how long each took, next to the app's last requests
void showXtreamConnectionTest();

}  // namespace tsvitch
