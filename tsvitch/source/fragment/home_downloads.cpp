#include "fragment/home_downloads.hpp"

#include <borealis/core/application.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/views/dialog.hpp>

#include "view/auto_tab_frame.hpp"
#include "view/download_item_cell.hpp"
#include "view/recycling_grid.hpp"
#include "utils/activity_helper.hpp"

using namespace brls::literals;
using namespace std::chrono_literals;

class DownloadDataSource : public RecyclingGridDataSource {
public:
    explicit DownloadDataSource(HomeDownloads* parent) : parent(parent) {}

    size_t getItemCount() override { return items.size(); }

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        auto* cell = dynamic_cast<DownloadItemCell*>(recycler->dequeueReusableCell("DownloadItemCell"));
        if (cell && index < items.size()) cell->setDownloadItem(items[index]);
        return cell;
    }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override { parent->onDownloadSelected(index); }

    void clearData() override { items.clear(); }

    std::vector<DownloadItem> items;

private:
    HomeDownloads* parent;
};

HomeDownloads::HomeDownloads() {
    this->inflateFromXMLRes("xml/fragment/home_downloads.xml");
    recyclingGrid->registerCell("DownloadItemCell", DownloadItemCell::create);
    dataSource = new DownloadDataSource(this);
    recyclingGrid->setDataSource(dataSource);
    DownloadManager::instance().loadDownloads();
}

HomeDownloads::~HomeDownloads() = default;

brls::View* HomeDownloads::create() { return new HomeDownloads(); }

void HomeDownloads::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                         brls::FrameContext* ctx) {
    auto now = std::chrono::steady_clock::now();
    if (now - lastCheck >= 500ms) {
        lastCheck = now;
        if (DownloadManager::instance().getVersion() != shownVersion) refreshList();
    }
    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

void HomeDownloads::refreshList() {
    auto& manager = DownloadManager::instance();
    shownVersion  = manager.getVersion();
    auto list     = manager.getAllDownloads();

    bool sameRows = list.size() == dataSource->items.size();
    for (size_t i = 0; sameRows && i < list.size(); i++) sameRows = list[i].id == dataSource->items[i].id;
    dataSource->items = list;

    emptyBox->setVisibility(list.empty() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    recyclingGrid->setVisibility(list.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);

    if (sameRows) {
        // Only the state or the progress changed: the rows on screen are updated in place
        for (auto* item : recyclingGrid->getGridItems()) {
            auto* cell = dynamic_cast<DownloadItemCell*>(item);
            if (cell && cell->getIndex() < list.size()) cell->setDownloadItem(list[cell->getIndex()]);
        }
        return;
    }

    // A download was added or removed: rebuild the list and keep the focus on a row that still exists
    brls::View* focus = brls::Application::getCurrentFocus();
    bool hadFocus     = false;
    size_t index      = 0;
    for (brls::View* v = focus; v; v = v->getParent()) {
        if (v == recyclingGrid) {
            hadFocus = true;
            break;
        }
    }
    if (hadFocus) {
        auto* cell = dynamic_cast<RecyclingGridItem*>(focus);
        if (cell) index = cell->getIndex();
    }
    if (!list.empty()) recyclingGrid->setDefaultCellFocus(std::min(index, list.size() - 1));
    recyclingGrid->reloadData();
    if (!hadFocus) return;
    if (list.empty())
        AutoTabFrame::focus2Sidebar(this);
    else
        brls::Application::giveFocus(recyclingGrid->getDefaultFocus());
}

void HomeDownloads::onDownloadSelected(size_t index) {
    if (index >= dataSource->items.size()) return;
    // The list on screen can be half a second old: the actions use the current state
    DownloadItem item = DownloadManager::instance().getDownload(dataSource->items[index].id);
    if (item.id.empty()) return;

    auto* dialog = new brls::Dialog(item.title);
    std::string id = item.id;
    switch (item.status) {
        case DownloadStatus::DOWNLOADING:
        case DownloadStatus::PENDING:
            dialog->addButton("tsvitch/download/pause"_i18n, [id]() { DownloadManager::instance().pauseDownload(id); });
            break;
        case DownloadStatus::COMPLETED:
            dialog->addButton("tsvitch/download/play"_i18n, [this, item]() { play(item); });
            break;
        default:
            dialog->addButton("tsvitch/download/resume"_i18n, [id]() {
                // The IPTV account allows one connection: one download at a time
                DownloadItem active;
                if (DownloadManager::instance().getActiveDownload(active) && active.id != id) {
                    auto* busy = new brls::Dialog(brls::getStr("tsvitch/download/busy_download", active.title));
                    busy->addButton("hints/ok"_i18n, []() {});
                    busy->open();
                    return;
                }
                DownloadManager::instance().resumeDownload(id);
            });
            break;
    }
    // B closes the dialog
    dialog->addButton("tsvitch/download/delete"_i18n, [this, item]() { confirmDelete(item); });
    dialog->open();
}

void HomeDownloads::confirmDelete(const DownloadItem& item) {
    // "Cancel" comes first: it has the focus, so a quick A press deletes nothing
    auto* dialog = new brls::Dialog(brls::getStr("tsvitch/download/delete_confirm", item.title));
    dialog->addButton("tsvitch/download/close"_i18n, []() {});
    dialog->addButton("tsvitch/download/delete"_i18n, [this, id = item.id]() {
        DownloadManager::instance().deleteDownload(id);
        refreshList();
    });
    dialog->open();
}

void HomeDownloads::play(const DownloadItem& item) {
    FILE* file = fopen(item.localPath.c_str(), "rb");
    if (!file) {
        auto* dialog = new brls::Dialog("tsvitch/download/file_missing"_i18n);
        dialog->addButton("hints/ok"_i18n, []() {});
        dialog->open();
        return;
    }
    fclose(file);

    tsvitch::LiveM3u8 video;
    video.title = item.title;
    video.url   = "file://" + item.localPath;
    video.logo  = item.imageUrl;
    Intent::openLive({video}, 0, []() {});
}
