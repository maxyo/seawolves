// The nation relations screen in the touch UI (mobile_nation_relations.hpp): the relation matrix read from the
// script's images, the screen's widgets (CollectNations) and its actions (ApplyNationAction), which run only while
// the widgets that granted them are unchanged.
#include "core_fake.hpp"
#include "mobile_nation_relations.hpp"

#include <catch2/catch.hpp>

#include <algorithm>
#include <functional>

entid_t g_idInterface = 1;
static storm::test::FakeCore fake;
Core &core = fake;

namespace storm::mobile
{
// The picture service: the atlas entry as the image's source.
ImageRef AtlasReference(VXSERVICE *, const char *group, const char *image, uint32_t)
{
    ImageRef ref;
    ref.source = {"atlas", group, image, {}, {}, -1};
    return ref;
}
} // namespace storm::mobile

using storm::mobile::ActionOutcome;
using storm::mobile::CollectNationMatrix;
using storm::mobile::NationImage;

namespace
{
NationImage picture(int n, const std::string &group, const std::string &name, int x, int y)
{
    NationImage p;
    p.node = "pic" + std::to_string(n);
    p.caption = "caption:" + name;
    p.x = x;
    p.y = y;
    p.image.source = {"atlas", group, name, {}, {}, -1};
    return p;
}
// The game's matrix: five nations across, the same five down, a relation picture at every pair but the diagonal.
std::vector<NationImage> matrixPictures()
{
    std::vector<NationImage> images;
    const std::string ids[] = {"France", "Pirate", "England", "Holland", "Spain"};
    int n = 1;
    for (int i = 0; i < 5; i++)
        images.push_back(picture(n++, "NATIONS", ids[i], 156 + 76 * i, 120));
    for (int i = 0; i < 5; i++)
        images.push_back(picture(n++, "NATIONS", ids[i], 75, 200 + 70 * i));
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 5; c++)
            if (r != c)
                images.push_back(picture(n++, "relations",
                                         r == 0   ? "Enemy"
                                         : r == 1 ? "Friend"
                                         : r == 2 ? "Unknown"
                                                  : "Neutral",
                                         156 + 76 * c, 200 + 70 * r));
    return images;
}

// The members of CINODE the screen reads; its widgets are given.
struct Node
{
    std::string name;
    std::vector<MobileWidget> model;
    bool m_bUse = true, m_bLockedNode = false, m_bLockStatus = false, m_bClickable = true, m_bSelected = true;
    int m_nDoDelay = 0, fired = 0, code = 0;
    std::vector<bool> firstPress; // DoAction's bFirstPress of every press
    std::function<void()> pressed;
    void MobileCollect(std::vector<MobileWidget> &out) const
    {
        out.insert(out.end(), model.begin(), model.end());
    }
    bool CheckCommandUsed(int) const
    {
        return true;
    }
    Node *DoAction(int action, bool &, bool first)
    {
        fired++;
        firstPress.push_back(first);
        code = action;
        if (pressed)
            pressed();
        return nullptr;
    }
};
// The strings service: the key itself.
struct Strings
{
    const char *GetString(const char *key) const
    {
        return key;
    }
};
// The members of XINTERFACE the screen reads and calls.
struct Interface
{
    struct IMAGE_Entity
    {
        struct
        {
            int left, top;
        } position;
        const char *sImageName, *sImageListName, *sPicture;
        IMAGE_Entity *next = nullptr;
    };
    IMAGE_Entity *m_imgLists = nullptr;
    std::vector<Node> *nodes = nullptr;
    VXSERVICE *pPictureService = nullptr;
    const Strings *pStringService = nullptr;
    std::vector<MobileWidget> m_mobileNationSignature;
    uint64_t m_mobileNationGeneration = 0;
    uint64_t opening = 9;
    int pickFailed = 0;
    Node *current = nullptr;

    Node *FindNode(const char *name, void *) const
    {
        for (auto &node : *nodes)
            if (storm::iEquals(node.name, std::string(name)))
                return &node;
        return nullptr;
    }
    void SetCurNode(Node *node)
    {
        current = node;
    }
    std::string MobileNationScore(const std::string &nation) const
    {
        return nation == "Spa" ? "-40" : "5";
    }
    std::string MobileNationFlags() const
    {
        return "England=0,France=1,Spain=1,Holland=0,Pirate=1";
    }
    void MobileNationPickFailed(const std::string &)
    {
        ++pickFailed;
    }
    // The nodes' widgets, then the nation screen's (XINTERFACE::MobileCollectWidgets on this screen).
    void MobileCollectWidgets(std::vector<MobileWidget> &widgets)
    {
        for (const auto &node : *nodes)
            if (node.m_bUse)
                node.MobileCollect(widgets);
        storm::mobile::CollectNations(*this, opening, widgets);
    }
};
struct Action
{
    std::string screen, node, value;
    int index;
};

// The stock nation relations screen: its nodes, the script's images and strings.
struct Screen
{
    Strings strings;
    std::vector<Node> nodes;
    std::vector<Interface::IMAGE_Entity> pictures;
    std::vector<std::string> names;
    ATTRIBUTES root{fake.scripts.codec};
    ATTRIBUTES *texts = nullptr;
    Interface xi;
    int closed = 0;

    Screen()
    {
        const auto node = [&](std::string name, std::string type, std::string label) {
            Node n;
            n.name = name;
            n.model.push_back({name, type, label, "", -1});
            nodes.push_back(n);
        };
        node("TITLE", "heading", "Relations");
        for (auto name : {"ENG_TEXT", "FRA_TEXT", "SPA_TEXT", "HOL_TEXT"})
            node(name, "text", "Native reputation");
        node("FlagPic", "image", "");
        storm::mobile::ImageRef flag;
        flag.source = {"atlas", "NATIONS", "France", {}, {}, -1};
        nodes.back().model[0].images.push_back(flag);
        node("FLAGS", "button", "Change");
        node("LEFTCHANGE_NATION", "button", "<");
        node("RIGHTCHANGE_NATION", "button", ">");
        node("INFO_CLICK", "text", "");
        xi.nodes = &nodes;
        xi.pStringService = &strings;

        names.reserve(34);
        pictures.reserve(34);
        const char *ids[] = {"England", "France", "Spain", "Holland", "Pirate"};
        int num = 1;
        const auto image = [&](const char *group, const char *name, int x, int y, const char *node = nullptr) {
            names.push_back(node ? node : "pic" + std::to_string(num++));
            pictures.push_back({{x, y}, names.back().c_str(), group, name});
        };
        for (int i = 0; i < 5; i++)
            image("NATIONS", ids[i], 156 + 76 * i, 120);
        for (int i = 0; i < 5; i++)
            image("NATIONS", ids[i], 75, 200 + 70 * i);
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 5; c++)
                if (r != c)
                    image("relations", r == 0 ? "Enemy" : "Neutral", 156 + 76 * c, 200 + 70 * r);
        image("NATIONS", "ENGLAND", 610, 136, "eng");
        image("NATIONS", "FRANCE", 610, 192, "FRA");
        image("NATIONS", "SPAIN", 610, 248, "spa");
        image("NATIONS", "HOLLAND", 610, 304, "hOl");
        for (size_t i = 0; i + 1 < pictures.size(); i++)
            pictures[i].next = &pictures[i + 1];
        xi.m_imgLists = &pictures[0];

        texts = root.CreateAttribute("strings", nullptr);
        for (auto key : {"EngH", "FraH", "SpaH", "HolH", "titul"})
            texts->SetAttribute(key, key);
        fake.entityAttributes[g_idInterface] = &root;
        fake.event = [this](std::string_view name) {
            REQUIRE(name == "MouseRClickUP");
            ++closed;
        };
    }
    ~Screen()
    {
        fake.entityAttributes.clear();
        fake.event = nullptr;
    }
    Node &named(const std::string &name)
    {
        return *xi.FindNode(name.c_str(), nullptr);
    }
    std::vector<MobileWidget> widgets()
    {
        std::vector<MobileWidget> v;
        xi.MobileCollectWidgets(v);
        return v;
    }
    MobileWidget control(const std::string &name, int index = -1)
    {
        for (auto &w : widgets())
            if (w.node == name && w.index == index)
                return w;
        FAIL("no widget " << name << " " << index);
        return {};
    }
    ActionOutcome apply(const std::string &node, const std::string &value, int index = -1)
    {
        return storm::mobile::ApplyNationAction(xi, &named(node), Action{"nationrelation", node, value, index});
    }
};
} // namespace

TEST_CASE("the matrix pairs every relation picture with its row and column by position, whatever the order")
{
    auto images = matrixPictures();
    images[0].node = "PIC1";
    std::vector<MobileWidget> result;
    REQUIRE(CollectNationMatrix(images, result));
    REQUIRE(result.size() == 6);
    CHECK(result[0].headerCells->at(0).images.at(0).source.image == "France");
    CHECK(result[0].headerCells->at(0).text == "caption:France");
    size_t relations = 0;
    for (size_t row = 1; row < result.size(); ++row)
        for (const auto &cell : result[row].cells)
            relations += cell.images.size();
    CHECK(relations == 20);
    CHECK(result[1].rowId == "France");
    CHECK(result[1].cells[0].text == "self");
    CHECK(result[1].cells[0].images.empty());
    CHECK(result[1].cells[1].images.at(0).source.image == "Enemy");
    CHECK(result[3].cells[0].images.at(0).source.image == "Unknown");
    CHECK(result[2].cells[0].images.at(0).source.image == "Friend");

    std::reverse(images.begin(), images.end());
    std::vector<MobileWidget> reversed;
    REQUIRE(CollectNationMatrix(images, reversed));
    CHECK(reversed == result);
}

TEST_CASE("a matrix that does not pair up is refused with a reason and adds nothing")
{
    // Refused with the reason that names the fault.
    const auto refused = [](std::vector<NationImage> images, const std::string &reason) {
        std::vector<MobileWidget> out;
        std::string why;
        CHECK_FALSE(CollectNationMatrix(images, out, &why));
        CHECK(out.empty());
        CHECK(why.rfind("matrix:" + reason, 0) == 0);
    };
    auto bad = matrixPictures();
    bad.pop_back(); // a relation missing
    refused(bad, "pair-count");
    bad = matrixPictures();
    bad.push_back(picture(31, "relations", "Enemy", 100, 200)); // one more than the pairs
    refused(bad, "pair-count");
    bad = matrixPictures();
    bad[0].image.source.image = bad[1].image.source.image; // a nation twice across
    refused(bad, "axis-schema-or-caption");
    bad = matrixPictures();
    bad[0].caption = "";
    refused(bad, "axis-schema-or-caption");
    bad = matrixPictures();
    bad[10].x = 999; // a relation off every column
    refused(bad, "missing-or-reused-pair");
    bad = matrixPictures();
    bad[11].y = bad[10].y;
    bad[11].x = bad[10].x; // two relations on the first pair
    refused(bad, "duplicate-pair");
    bad = matrixPictures();
    bad.push_back(picture(31, "OTHER", "Flag", 600, 600)); // a picture of neither the axes nor the relations
    refused(bad, "unexpected-group");
    bad = matrixPictures();
    bad.push_back(picture(31, "NATIONS", "Sweden", 900, 120)); // an odd number of axis pictures
    refused(bad, "axis-image-count");
    // Pictures that are not numbered picN are not part of the matrix.
    bad = matrixPictures();
    auto other = picture(1, "OTHER", "Flag", 600, 600);
    other.node = "FlagPic";
    bad.push_back(other);
    std::vector<MobileWidget> out;
    CHECK(CollectNationMatrix(bad, out));
}

TEST_CASE("the screen is complete only with every reputation caption; reputations carry their score")
{
    Screen s;
    CHECK(s.control("ANDROID_NATION_RELATIONS").value == "1");
    CHECK(s.control("ANDROID_NATION_RELATIONS").label == "titul");
    int scored = 0, reputations = 0;
    for (auto &w : s.widgets())
        if (w.type == "nation_reputation")
        {
            ++reputations;
            scored += w.context.at("score") == "-40";
        }
    CHECK(reputations == 4);
    CHECK(scored == 1);
    CHECK(s.control("FlagPic").context.at("flags") == "England=0,France=1,Spain=1,Holland=0,Pirate=1");
    CHECK(s.control("FlagPic").label == "France");

    s.texts->SetAttribute("EngH", "");
    const auto missing = s.control("ANDROID_NATION_RELATIONS");
    CHECK(missing.value == "0");
    CHECK(missing.unavailableReason.find("Eng:caption-missing") != std::string::npos);
    size_t sources = 0;
    for (auto &w : s.widgets())
        sources += w.type == "nation_source";
    CHECK(sources == s.pictures.size());
}

TEST_CASE("a button's action runs once, and only while the widgets that granted it are unchanged")
{
    Screen s;
    auto &flags = s.named("FLAGS");
    REQUIRE(s.control("FLAGS").actions.size() == 1);
    auto token = s.control("FLAGS").actions[0];
    CHECK(token.rfind("nation@9:", 0) == 0);
    CHECK(s.apply("FLAGS", token).state == ActionOutcome::Applied);
    CHECK(flags.fired == 1);
    CHECK(s.xi.current == &flags);
    CHECK(s.apply("FLAGS", token).state == ActionOutcome::Refused); // used up
    CHECK(flags.fired == 1);

    token = s.control("FLAGS").actions[0];
    s.named("FlagPic").model[0].images[0].source.image = "Pirate"; // the candidate changed
    CHECK(s.apply("FLAGS", token).state == ActionOutcome::Refused);
    token = s.control("FLAGS").actions[0];
    s.xi.opening++; // another opening of the screen
    CHECK(s.apply("FLAGS", token).state == ActionOutcome::Refused);
    token = s.control("FLAGS").actions[0];
    flags.m_bSelected = false;
    CHECK(s.apply("FLAGS", token).state == ActionOutcome::Refused);
    CHECK(s.control("FLAGS").actions.empty());
    flags.m_bSelected = true;
    token = s.control("FLAGS").actions[0];
    flags.m_nDoDelay = 150;
    CHECK(s.apply("FLAGS", token).state == ActionOutcome::Refused);
    flags.m_nDoDelay = 0;
    token = s.control("FLAGS").actions[0];
    flags.m_bLockStatus = true;
    CHECK(s.apply("FLAGS", token).state == ActionOutcome::Refused);
    flags.m_bLockStatus = false;
    // A node that cannot be pressed now offers no action.
    for (bool Node::*flag : {&Node::m_bLockedNode, &Node::m_bLockStatus})
    {
        flags.*flag = true;
        CHECK(s.control("FLAGS").actions.empty());
        CHECK_FALSE(s.control("FLAGS").enabled);
        flags.*flag = false;
    }
    flags.m_nDoDelay = 150;
    CHECK(s.control("FLAGS").actions.empty());
    flags.m_nDoDelay = 0;
    CHECK(s.apply("FLAGS", token + "x").state == ActionOutcome::Refused);
    CHECK(s.apply("FLAGS", token, 0).state == ActionOutcome::Refused); // another widget of the node
    CHECK(flags.fired == 1);
    CHECK(s.apply("FLAGS", s.control("FLAGS").actions[0]).state == ActionOutcome::Applied);
    CHECK(flags.fired == 2);
}

TEST_CASE("help is the node's right click; a locked node offers none; the help closes by the script's event")
{
    Screen s;
    auto token = s.control("INFO_CLICK", -2).actions.at(0);
    CHECK(s.apply("INFO_CLICK", token, -2).state == ActionOutcome::Applied);
    CHECK(s.named("INFO_CLICK").fired == 1);
    CHECK(s.named("INFO_CLICK").code == ACTION_MOUSERCLICK);

    token = s.control("FlagPic", -2).actions.at(0);
    s.named("FlagPic").m_bLockedNode = true;
    CHECK(s.control("FlagPic", -2).actions.empty());
    CHECK(s.apply("FlagPic", token, -2).state == ActionOutcome::Refused);
    CHECK(s.named("FlagPic").fired == 0);
    s.named("FlagPic").m_bLockedNode = false;
    s.named("FlagPic").m_bClickable = false; // the game takes no click on it now
    CHECK(s.control("FlagPic", -2).actions.empty());
    s.named("FlagPic").m_bClickable = true;

    Node fader;
    fader.name = "tooltip_fader";
    s.nodes.push_back(fader);
    const auto close = s.control("tooltip_fader", -3);
    REQUIRE(close.actions.size() == 1);
    CHECK(s.apply("tooltip_fader", close.actions[0], -3).state == ActionOutcome::Applied);
    CHECK(s.closed == 1);
    CHECK(s.named("tooltip_fader").fired == 0);
}

TEST_CASE("a flag pick presses › until the candidate is the flag, at most once round, and says when it was not")
{
    Screen s;
    auto &right = s.named("RIGHTCHANGE_NATION");
    auto pick = s.control("RIGHTCHANGE_NATION").actions.back();
    REQUIRE(pick.size() > 5);
    CHECK(pick.compare(pick.size() - 5, 5, ":pick") == 0);
    CHECK(s.apply("RIGHTCHANGE_NATION", pick + "=france").state == ActionOutcome::Applied);
    CHECK(right.fired == 0); // already the candidate
    CHECK(s.xi.pickFailed == 0);

    pick = s.control("RIGHTCHANGE_NATION").actions.back();
    // The candidate never reaches Spain: eight presses, then the failure is reported.
    CHECK(s.apply("RIGHTCHANGE_NATION", pick + "=Spain").state == ActionOutcome::Refused);
    CHECK(right.fired == 8);
    CHECK(s.xi.pickFailed == 1);
    CHECK(s.apply("RIGHTCHANGE_NATION", pick + "=Spain").state == ActionOutcome::Refused); // used up
    CHECK(right.fired == 8);

    // Each press shows the next nation, as the script does.
    auto &candidate = s.named("FlagPic").model[0].images[0].source.image;
    right.pressed = [&] {
        static const std::vector<std::string> order = {"France", "England", "Spain", "Holland", "Pirate"};
        const auto at = std::find(order.begin(), order.end(), candidate);
        candidate = order[(at - order.begin() + 1) % order.size()];
    };
    right.fired = 0;
    right.firstPress.clear();
    pick = s.control("RIGHTCHANGE_NATION").actions.back();
    CHECK(s.apply("RIGHTCHANGE_NATION", pick + "=SPAIN").state == ActionOutcome::Applied);
    CHECK(right.fired == 2);
    CHECK(right.firstPress == std::vector<bool>{true, false}); // the presses are one gesture
    CHECK(candidate == "Spain");
    CHECK(s.xi.current == &right);
    CHECK(s.xi.pickFailed == 1);
}
