#pragma once

namespace tsvitch {

/// Asks the server for the subscription (status, end date, connections) and shows it in a dialog
void showXtreamAccountInfo();

/// At most once a day: reminds the user when the subscription ends within a week
void checkXtreamExpiry();

}  // namespace tsvitch
