// What the touch UI gets of a table node (MobileTableCollect, MobileTableApply in mobile_table.hpp): rows with their
// cells, header row and stable identities, an empty table as one widget, and touch actions accepted only for the row
// identity and screen opening they were made for.
#include "core_fake.hpp"
#include "mobile_table.hpp"

#include <catch2/catch.hpp>

#include <deque>
#include <map>

entid_t g_idInterface = 1;
static storm::test::FakeCore fake;
Core &core = fake;

// The picture service: one handle per atlas entry or texture; "missing" is an entry the atlas does not have.
namespace storm::mobile
{
static uint64_t imageHandle(const std::string &key)
{
    static std::map<std::string, uint64_t> handles;
    const auto [entry, inserted] = handles.try_emplace(key, handles.size() + 1);
    return entry->second;
}
ImageRef AtlasReference(VXSERVICE *, const char *group, const char *image, uint32_t)
{
    if (!group || !*group || !image || !*image || std::string(image) == "missing")
        return {};
    ImageRef ref;
    ref.handle = imageHandle(std::string(group) + ":" + image);
    ref.imageRevision = 9;
    ref.source = {"atlas", group, image, {}, {}, -1};
    return ref;
}
ImageRef Reference(Descriptor descriptor, uint32_t)
{
    ImageRef ref;
    ref.handle = imageHandle(descriptor.path);
    ref.imageRevision = 9;
    return ref;
}
} // namespace storm::mobile

namespace
{
struct Strings
{
    std::map<std::string, std::string> translations;
    const char *GetString(const char *key) const
    {
        const auto value = translations.find(key);
        return value == translations.end() ? nullptr : value->second.c_str();
    }
};
// The members of CXI_TABLE the templates read and call.
struct Table
{
    const char *m_nodeName = "TABLE_LIST";
    int m_nLineQuantity = 2, m_nColQuantity = 3, m_nSelectIndex = 0;
    struct
    {
        int top = 0, left = 0;
    } m_rect;
    bool m_bClickable = true, m_bLockStatus = false, m_bUse = true, m_bLockedNode = false;
    uint32_t m_dwFontCellColor = 0xffeeeeee;
    VXSERVICE *pPictureService = nullptr;
    const Strings *pStringService = nullptr;
    std::string mobileRefusal_;
    int activated = 0;
    bool activateCommand = true; // the node has an ACTION_ACTIVATE or ACTION_MOUSEDBLCLICK command
    void SetTopIndexForSelect(int)
    {
    }
    void SelectLine(int row)
    {
        m_nSelectIndex = row;
    }
    void DoAction(int, bool &, bool)
    {
        ++activated;
    }
    bool CheckCommandUsed(int command) const
    {
        return activateCommand && (command == ACTION_ACTIVATE || command == ACTION_MOUSEDBLCLICK);
    }
};

struct Fixture
{
    storm::test::Codec &codec = fake.scripts.codec;
    ATTRIBUTES root{codec};
    Table table;
    std::string screen = "test_screen";
    std::deque<storm::ui::Action> actions;
    std::vector<std::string> focused; // MSG_INTERFACE_NEW_CURRENT_NODE targets

    Fixture()
    {
        fake.entityAttributes[g_idInterface] = &root;
        fake.message = [this](entid_t to, const std::vector<std::string> &arguments) -> uint64_t {
            REQUIRE(to == g_idInterface);
            REQUIRE(arguments.at(0) == std::to_string(MSG_INTERFACE_NEW_CURRENT_NODE));
            focused.push_back(arguments.at(1));
            return 0;
        };
    }
    ~Fixture()
    {
        fake.entityAttributes.clear();
        fake.message = nullptr;
    }
    std::vector<MobileWidget> collect()
    {
        std::vector<MobileWidget> widgets;
        MobileTableCollect(table, screen, widgets);
        return widgets;
    }
    bool apply(int index, const std::string &verb, const std::string &value)
    {
        storm::ui::Action action;
        action.screen = screen;
        action.node = table.m_nodeName;
        action.index = index;
        action.verb = verb;
        action.value = value;
        return MobileTableApply(table, action, actions);
    }
};

std::vector<int> columns(const std::vector<MobileCell> &cells)
{
    std::vector<int> result;
    for (const auto &cell : cells)
        result.push_back(cell.column);
    return result;
}
} // namespace

TEST_CASE("rows carry their cells and identity; the header row is present only with hr")
{
    Fixture f;
    auto *data = f.root.CreateAttribute("TABLE_LIST", nullptr);
    for (int row = 1; row <= 2; ++row)
    {
        auto *line = data->CreateAttribute(("tr" + std::to_string(row)).c_str(), nullptr);
        line->CreateAttribute("td1", nullptr)->SetAttribute("str", "cargo");
        line->CreateAttribute("td3", nullptr)->SetAttribute("str", "20");
        line->CreateAttribute("UserData", nullptr)->SetAttribute("ID", ("cargo:" + std::to_string(row)).c_str());
    }
    const auto collect = [&] {
        auto widgets = f.collect();
        REQUIRE(widgets.size() == 2);
        for (int row = 0; row < 2; ++row)
        {
            CHECK(widgets[row].index == row);
            CHECK(widgets[row].type == "choice");
            REQUIRE(widgets[row].cells.size() == 3);
            CHECK(widgets[row].cells[1].text.empty());
            CHECK(widgets[row].cells[2].column == 2);
            CHECK(widgets[row].cells[2].text == "20");
            CHECK(widgets[row].label == "cargo\t\t20");
            CHECK(widgets[row].rowId == "cargo:" + std::to_string(row + 1));
            CHECK(widgets[row].actions == std::vector<std::string>{"select", "activate"});
        }
        CHECK(widgets[0].value == "1");
        CHECK(widgets[1].value == "0");
        return widgets;
    };
    const auto plain = collect();
    for (const auto &row : plain)
    {
        CHECK(row.columnHeaders.empty());
        CHECK_FALSE(row.headerCells.has_value());
    }

    auto *header = data->CreateAttribute("hr", nullptr);
    auto *first = header->CreateAttribute("td1", nullptr);
    first->SetAttribute("str", "Товар \"A\"\nшт.");
    auto *last = header->CreateAttribute("td3", nullptr);
    last->SetAttribute("str", "Вес");
    header->CreateAttribute("td4", nullptr)->SetAttribute("str", "outside table");
    const auto sparse = collect();
    for (size_t row = 0; row < sparse.size(); ++row)
    {
        CHECK(sparse[row].columnHeaders == std::vector<std::string>{"Товар \"A\"\nшт.", "", "Вес"});
        REQUIRE(sparse[row].headerCells.has_value());
        CHECK(columns(*sparse[row].headerCells) == std::vector<int>{0, 2});
        auto body = sparse[row];
        body.columnHeaders.clear();
        body.headerCells.reset();
        CHECK(body == plain[row]); // the header row changes nothing else
    }

    first->SetAttribute("str", "");
    header->CreateAttribute("td2", nullptr); // a present cell without str is also empty
    last->SetAttribute("str", "Новый вес");
    const auto changed = collect();
    for (const auto &row : changed)
    {
        CHECK(row.columnHeaders == std::vector<std::string>{"", "", "Новый вес"});
        CHECK(columns(*row.headerCells) == std::vector<int>{0, 1, 2});
    }

    data->DeleteAttributeClassX(header);
    for (const auto &row : collect())
    {
        CHECK(row.columnHeaders.empty());
        CHECK_FALSE(row.headerCells.has_value());
    }
}

TEST_CASE("a table without rows is one 'table' widget with its headers and no verbs; no attributes, no widget")
{
    Fixture f;
    f.root.CreateAttribute("TABLE_LIST", nullptr)
        ->CreateAttribute("hr", nullptr)
        ->CreateAttribute("td1", nullptr)
        ->SetAttribute("str", "Город");
    auto empty = f.collect();
    REQUIRE(empty.size() == 1);
    CHECK(empty[0].type == "table");
    CHECK(empty[0].index == -1);
    CHECK(empty[0].actions.empty());
    CHECK(empty[0].columnHeaders == std::vector<std::string>{"Город", "", ""});
    CHECK(empty[0].headerCells.has_value());

    f.table.m_nodeName = "TABLE_OTHER";
    CHECK(f.collect().empty());
}

TEST_CASE("header captions come from the actual nation image of a cell, never from its column")
{
    Fixture f;
    auto *nation = f.root.CreateAttribute("TABLE_NATION", nullptr);
    auto *nationHeader = nation->CreateAttribute("hr", nullptr);
    const std::string zetaImage = "Zeta\"\\\n";
    for (int row = 1; row <= 2; ++row)
    {
        auto *line = nation->CreateAttribute(("tr" + std::to_string(row)).c_str(), nullptr);
        line->CreateAttribute("td1", nullptr)->SetAttribute("str", "value");
        auto *bodyIcon = line->CreateAttribute("td3", nullptr)->CreateAttribute("icon", nullptr);
        bodyIcon->SetAttribute("group", "NATIONS");
        bodyIcon->SetAttribute("image", zetaImage.c_str());
    }
    f.table.m_nodeName = "TABLE_NATION";
    f.table.m_nColQuantity = 7;
    Strings strings;
    strings.translations[zetaImage] = "Реальная \"страна\"\n";
    strings.translations["Empty"] = "";
    f.table.pStringService = &strings;
    const auto nationRows = [&] {
        auto rows = f.collect();
        REQUIRE(rows.size() == 2);
        REQUIRE(rows[0].headerCells.has_value());
        CHECK(rows[0].cells[2].images.at(0).source.image == zetaImage);
        CHECK(rows[0].actions == std::vector<std::string>{"select", "activate"});
        return rows;
    };
    for (const auto &row : nationRows()) // hr present, no td cells
        CHECK(row.headerCells == std::vector<MobileCell>{});

    nationHeader->CreateAttribute("td1", nullptr)->SetAttribute("str", "Actual heading");
    const auto icon = [&](int column, const char *name, const char *image) {
        const auto key = "td" + std::to_string(column);
        auto *cell = nationHeader->GetAttributeClass(key.c_str());
        if (!cell)
            cell = nationHeader->CreateAttribute(key.c_str(), nullptr);
        auto *source = cell->CreateAttribute(name, nullptr);
        source->SetAttribute("group", "NATIONS");
        source->SetAttribute("image", image);
        return source;
    };
    auto *zeta = icon(2, "icon", zetaImage.c_str());
    zeta->GetParent()->SetAttributeUseDword("color", 0xff123456);
    auto *alpha = icon(3, "icon", "Alpha"); // no translation: the image identity stays, no caption
    icon(4, "icon", zetaImage.c_str());     // a duplicate identity is exported as such
    nationHeader->CreateAttribute("td5", nullptr)->CreateAttribute("icon", nullptr); // an icon without a picture
    icon(6, "icon1", zetaImage.c_str());
    icon(6, "icon2", "Alpha"); // two images: no caption
    icon(7, "icon", zetaImage.c_str())->GetParent()->SetAttribute("str", "Explicit caption");
    const auto nations = nationRows();
    const auto &headers = *nations[0].headerCells;
    REQUIRE(headers.size() == 7);
    CHECK(headers[1].column == 1);
    REQUIRE(headers[1].images.size() == 1);
    CHECK(headers[1].images[0].source == storm::mobile::ImageRef::Source{"atlas", "NATIONS", zetaImage, {}, {}, -1});
    CHECK(headers[1].images[0].handle == nations[0].cells[2].images[0].handle);
    CHECK(headers[1].text == "Реальная \"страна\"\n");
    CHECK(headers[1].colorArgb == 0xff123456);
    CHECK(headers[0].colorArgb == 0xffeeeeee);
    CHECK(headers[2].text.empty());
    CHECK(headers[2].images.at(0).source.image == "Alpha");
    CHECK(headers[3].images == headers[1].images);
    CHECK(headers[4].images.empty());
    CHECK(headers[5].images.size() == 2);
    CHECK(headers[5].text.empty());
    CHECK(headers[6].text == "Explicit caption");
    CHECK(nations[0].columnHeaders ==
          std::vector<std::string>{"Actual heading", "", "", "", "", "", "Explicit caption"});

    zeta->SetAttribute("image", "Alpha");
    alpha->SetAttribute("image", zetaImage.c_str());
    const auto reordered = nationRows();
    for (size_t row = 0; row < reordered.size(); ++row)
    {
        auto before = nations[row], after = reordered[row];
        CHECK((*after.headerCells)[1].images == (*before.headerCells)[2].images);
        CHECK((*after.headerCells)[2].images == (*before.headerCells)[1].images);
        CHECK((*after.headerCells)[2].text == (*before.headerCells)[1].text);
        CHECK((*after.headerCells)[1].text.empty());
        before.headerCells.reset();
        after.headerCells.reset();
        CHECK(before == after);
    }

    nationHeader->DeleteAttributeClassX(nationHeader->GetAttributeClass("td4"));
    nationHeader->DeleteAttributeClassX(nationHeader->GetAttributeClass("td6"));
    alpha->SetAttribute("image", "missing");
    const auto gaps = nationRows();
    CHECK(columns(*gaps[0].headerCells) == std::vector<int>{0, 1, 2, 4, 6});
    CHECK((*gaps[0].headerCells)[2].images.empty());

    alpha->SetAttribute("image", "Empty"); // an empty translation is no caption
    CHECK((*nationRows()[0].headerCells)[2].text.empty());
    alpha->SetAttribute("image", zetaImage.c_str());
    CHECK((*nationRows()[0].headerCells)[2].text == "Реальная \"страна\"\n");
    f.table.pStringService = nullptr;
    CHECK((*nationRows()[0].headerCells)[2].text.empty());

    // A texture icon needs four numbers in its uv.
    auto *texture = nationHeader->GetAttributeClass("td5");
    texture->DeleteAttributeClassX(texture->GetAttributeClass("icon"));
    auto *textureIcon = texture->CreateAttribute("icon", nullptr);
    textureIcon->SetAttribute("texture", "actual.tga");
    textureIcon->SetAttribute("uv", "bad");
    CHECK((*nationRows()[0].headerCells)[3].images.empty());
    textureIcon->SetAttribute("uv", "0,0,1,1");
    CHECK((*nationRows()[0].headerCells)[3].images.size() == 1);

    // Image-only rows are content (the UI draws their pictures); rows with neither text nor image are not, and a
    // header never makes an empty body visible.
    for (int row = 1; row <= 2; ++row)
        nation->GetAttributeClass(("tr" + std::to_string(row)).c_str())
            ->GetAttributeClass("td1")
            ->SetAttribute("str", "");
    CHECK(f.collect().size() == 2);
    for (int row = 1; row <= 2; ++row)
    {
        auto *cell = nation->GetAttributeClass(("tr" + std::to_string(row)).c_str())->GetAttributeClass("td3");
        cell->DeleteAttributeClassX(cell->GetAttributeClass("icon"));
    }
    const auto onlyTable = f.collect();
    REQUIRE(onlyTable.size() == 1);
    CHECK(onlyTable[0].type == "table");
}

TEST_CASE("trade rows are acted on only with the identity of the good and its owner and the screen's opening")
{
    // The stock commerce scripts: row.index and the selector's current picN owner; no UserData.ID.
    Fixture f;
    auto *goods = f.root.CreateAttribute("TABLE_LIST", nullptr);
    auto *item = goods->CreateAttribute("tr1", nullptr);
    item->SetAttribute("index", "42");
    item->CreateAttribute("td1", nullptr)->SetAttribute("str", "commodity");
    auto *ships = f.root.CreateAttribute("SHIPS_SCROLL", nullptr);
    ships->SetAttribute("current", "0");
    ships->CreateAttribute("pic1", nullptr)->SetAttribute("companionIndex", "7");
    auto *people = f.root.CreateAttribute("CHARACTERS_SCROLL", nullptr);
    people->SetAttribute("current", "0");
    people->CreateAttribute("pic1", nullptr)->SetAttribute("character", "8");
    f.table.m_nLineQuantity = 1;
    for (const std::string screen :
         {"store", "storage", "contraband", "cannons", "itemstrade", "itemsbox", "colonycapture"})
    {
        INFO(screen);
        f.screen = screen;
        f.actions.clear();
        const bool personal = screen == "itemstrade" || screen == "itemsbox";
        const auto id = storm::mobile::InterfaceTableRowId(screen, "TABLE_LIST", item, &f.root);
        CHECK(id == "commerce:" + screen + ":item:42:owner:" + (personal ? "8" : "7") + ":role:0");
        auto live = f.collect();
        REQUIRE(live.size() == 1);
        CHECK(live[0].rowId == id);
        CHECK(live[0].enabled);
        CHECK_FALSE(f.apply(0, "select", "")); // no unversioned select for a trade table
        CHECK_FALSE(f.apply(0, "activate", ""));
        CHECK(f.apply(0, "select", id));
        const int activated = f.table.activated;
        CHECK(f.apply(0, "activate", id));
        CHECK(f.table.activated == activated); // an activation is queued, not run
        REQUIRE(f.actions.size() == 1);
        CHECK((f.actions.front().verb == "dispatch" && f.actions.front().value == id));
        CHECK(f.actions.front().node == "TABLE_LIST");
        CHECK(f.actions.front().screen == screen);
        item->SetAttribute("index", "43");
        CHECK_FALSE(f.apply(0, "dispatch", id)); // the row was refilled by the callback
        item->SetAttribute("index", "42");
        auto *selector = personal ? people : ships;
        auto *owner = selector->GetAttributeClass("pic1");
        const char *field = personal ? "character" : "companionIndex";
        owner->SetAttribute(field, "90");
        CHECK_FALSE(f.apply(0, "select", id)); // the same good of another owner
        owner->SetAttribute(field, personal ? "8" : "7");
        selector->SetAttribute("current", "1");
        CHECK_FALSE(f.apply(0, "select", id)); // no selector entry
        live = f.collect();
        CHECK_FALSE(live[0].enabled);
        CHECK(live[0].actions.empty());
        selector->SetAttribute("current", "0");
        f.table.m_bLockedNode = true;
        CHECK_FALSE(f.apply(0, "select", id));
        f.table.m_bLockedNode = false;
        CHECK(f.apply(0, "dispatch", id));
        CHECK(f.table.activated == activated + 1);

        // A ship switch refills the table in the game's next frame: a tap with the identity of the list before the
        // refill is refused, and the refusal names both identities.
        item->SetAttribute("index", "44");
        CHECK_FALSE(f.apply(0, "activate", id));
        const auto now = storm::mobile::InterfaceTableRowId(screen, "TABLE_LIST", item, &f.root);
        CHECK(f.table.mobileRefusal_ == "'activate " + id + "' is not for tr1 now (" + now + ")");
        item->SetAttribute("index", "42");
        CHECK_FALSE(f.apply(1, "activate", id));
        CHECK(f.table.mobileRefusal_ == "row 1 is outside the table's 1 rows");
        f.table.m_bLockedNode = true;
        CHECK_FALSE(f.apply(0, "activate", id));
        CHECK(f.table.mobileRefusal_ == "the table is locked by a window");
        f.table.m_bLockedNode = false;
        f.table.m_nSelectIndex = 0;
        CHECK(f.apply(0, "dispatch", id));
        CHECK(f.table.mobileRefusal_.empty());
    }
    // A dispatch for a row that is no longer the selected one is refused.
    f.table.m_nLineQuantity = 2;
    auto *second = goods->CreateAttribute("tr2", nullptr);
    second->SetAttribute("index", "42");
    f.table.m_nSelectIndex = 1;
    const auto id = storm::mobile::InterfaceTableRowId(f.screen, "TABLE_LIST", item, &f.root);
    CHECK_FALSE(f.apply(0, "dispatch", id));
    CHECK(f.table.mobileRefusal_ == "row 1 is selected, not 0");

    goods->SetAttribute("select", "1");
    const auto quantityId = storm::mobile::InterfaceTableRowId("store", "TABLE_LIST", item, &f.root);
    const auto guarded = quantityId;
    CHECK(storm::mobile::CommerceQuantityGuard("store", "QTY_OK_BUTTON", &f.root, guarded));
    CHECK(storm::mobile::CommerceQuantityGuard("store", "qty_edit", &f.root, guarded));
    CHECK_FALSE(storm::mobile::CommerceQuantityGuard("items", "QTY_OK_BUTTON", &f.root, guarded));
    CHECK_FALSE(storm::mobile::CommerceQuantityGuard("store", "I_SHIP", &f.root, guarded));
    CHECK_FALSE(storm::mobile::CommerceQuantityGuard("store", "QTY_OK_BUTTON", &f.root, ""));
    CHECK_FALSE(storm::mobile::CommerceQuantityGuard("store", "QTY_OK_BUTTON", &f.root, guarded + "|press"));
    item->SetAttribute("index", "43");
    CHECK_FALSE(storm::mobile::CommerceQuantityGuard("store", "QTY_OK_BUTTON", &f.root, guarded));
    item->SetAttribute("index", "42");
    ships->GetAttributeClass("pic1")->SetAttribute("companionIndex", "99");
    CHECK_FALSE(storm::mobile::CommerceQuantityGuard("store", "QTY_OK_BUTTON", &f.root, guarded));
    ships->GetAttributeClass("pic1")->SetAttribute("companionIndex", "7");
    goods->SetAttribute("select", "0");
    CHECK_FALSE(storm::mobile::CommerceQuantityGuard("store", "QTY_OK_BUTTON", &f.root, guarded));

    f.screen = "store";
    f.table.m_nLineQuantity = 1;
    f.table.m_nSelectIndex = 0;
    for (const char *bad : {"", "-1", "+1", "42x", "4294967296"})
    {
        INFO(bad);
        item->SetAttribute("index", bad);
        CHECK(storm::mobile::InterfaceTableRowId("store", "TABLE_LIST", item, &f.root).empty());
        CHECK_FALSE(f.apply(0, "activate", ""));
        CHECK(f.table.mobileRefusal_ == "tr1 has no identity");
    }
}

TEST_CASE("a reference table's row is its UserData identity, and a selection focuses the table")
{
    Fixture f;
    auto *cities = f.root.CreateAttribute("TABLE_CITY", nullptr);
    auto *city = cities->CreateAttribute("tr1", nullptr);
    city->CreateAttribute("td1", nullptr)->SetAttribute("str", "Actual city");
    auto *cityData = city->CreateAttribute("UserData", nullptr);
    cityData->SetAttribute("CityID", "FortFrance");
    f.table.m_nodeName = "TABLE_CITY";
    f.table.m_nLineQuantity = 1;
    f.screen = "tradebook";
    const std::string cityId = "tradebook:city:FortFrance";
    CHECK(storm::mobile::InterfaceTableRowId("tradebook", "TABLE_CITY", city, &f.root) == cityId);
    // The journal's page of the trade book lists the same towns.
    CHECK(storm::mobile::InterfaceTableRowId("questbook", "TABLE_CITY", city, &f.root) == cityId);
    auto cityRows = f.collect();
    REQUIRE(cityRows.size() == 1);
    CHECK(cityRows[0].rowId == cityId);
    CHECK(f.focused.empty());
    CHECK(f.apply(0, "select", cityId));
    CHECK(f.focused == std::vector<std::string>{"TABLE_CITY"});
    CHECK_FALSE(f.apply(0, "select", ""));
    cityData->SetAttribute("CityID", "PortRoyal");
    CHECK_FALSE(f.apply(0, "select", cityId));
    CHECK(f.apply(0, "select", "tradebook:city:PortRoyal"));
    for (const char *bad : {"", "wrong\ncity", "wrong|city", "wrong city"})
    {
        INFO(bad);
        cityData->SetAttribute("CityID", bad);
        CHECK_FALSE(f.apply(0, "select", cityId));
        cityRows = f.collect();
        CHECK_FALSE(cityRows[0].enabled);
        CHECK(cityRows[0].actions.empty());
    }
    cityData->SetAttribute("CityID", std::string(129, 'x').c_str());
    CHECK(storm::mobile::InterfaceTableRowId("tradebook", "TABLE_CITY", city, &f.root).empty());
    cityData->SetAttribute("ID", "Food");
    CHECK(storm::mobile::InterfaceTableRowId("tradebook", "TABLE_GOODS", city, &f.root) == "Food");
    CHECK_FALSE(storm::mobile::LeasedTableIdentityRequired("other", "TABLE_CITY"));

    // Without an activate command a row is only selected.
    cityData->SetAttribute("CityID", "PortRoyal");
    const std::string royal = "tradebook:city:PortRoyal";
    f.table.activateCommand = false;
    CHECK(f.collect()[0].actions == std::vector<std::string>{"select"});
    CHECK_FALSE(f.apply(0, "activate", royal));
    CHECK(f.table.mobileRefusal_ == "the table has no activate command");
    CHECK(f.actions.empty());
    f.table.activateCommand = true;
    CHECK(f.apply(0, "activate", royal));
    CHECK(f.actions.size() == 1);

    // A table that needs no identity takes a plain select; a disabled table takes nothing.
    f.screen = "other";
    CHECK(f.apply(0, "select", ""));
    f.table.m_bLockStatus = true;
    CHECK_FALSE(f.apply(0, "select", ""));
    CHECK(f.table.mobileRefusal_ == "the table is lock-status");
    f.table.m_bLockStatus = false;
    for (bool Table::*flag : {&Table::m_bUse, &Table::m_bClickable})
    {
        f.table.*flag = false;
        CHECK_FALSE(f.apply(0, "select", ""));
        f.table.*flag = true;
    }
}

TEST_CASE("a ship's hold row is the good of the ship shown, never a bare index")
{
    Fixture f;
    auto *holdShips = f.root.CreateAttribute("SHIPS_SCROLL", nullptr);
    holdShips->SetAttribute("current", "1");
    holdShips->CreateAttribute("pic2", nullptr)->SetAttribute("character", "31");
    auto *holdGood = f.root.CreateAttribute("TABLE_LIST", nullptr)->CreateAttribute("tr1", nullptr);
    holdGood->SetAttribute("index", "4");
    CHECK(storm::mobile::LeasedTableIdentityRequired("ship", "TABLE_LIST"));
    CHECK(storm::mobile::InterfaceTableRowId("ship", "TABLE_LIST", holdGood, &f.root) == "ship:good:4:owner:31");
    holdShips->SetAttribute("current", "0");
    CHECK(storm::mobile::InterfaceTableRowId("ship", "TABLE_LIST", holdGood, &f.root).empty());
}

TEST_CASE("a ship exchange's cargo row is its good")
{
    Fixture f;
    auto *good = f.root.CreateAttribute("TABLE_LIST", nullptr)->CreateAttribute("tr1", nullptr);
    good->SetAttribute("index", "26");
    CHECK(storm::mobile::LeasedTableIdentityRequired("transfer_main", "TABLE_LIST"));
    CHECK(storm::mobile::InterfaceTableRowId("transfer_main", "TABLE_LIST", good, &f.root) == "transfer:good:26");
    good->SetAttribute("index", "");
    CHECK(storm::mobile::InterfaceTableRowId("transfer_main", "TABLE_LIST", good, &f.root).empty());
}
