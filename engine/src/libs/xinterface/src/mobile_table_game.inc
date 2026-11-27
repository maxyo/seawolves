// Included inside xinterface.cpp's Android implementation: the facts of the tavern mini-games
// (PROGRAM/interface/boal_dice.c, boal_cards.c) for mobile_table_game.hpp: the variables of the game's window,
// which the touch interface's scripts cannot name. All rules stay in script.
namespace {
std::optional<std::string> TableInteger(const char *name){
    const auto n=MobileVariableInteger(name);
    return n?std::optional<std::string>(std::to_string(*n)):std::nullopt;
}
std::optional<std::string> TableAttribute(ATTRIBUTES *root,const char *path){
    // An attribute the script set to "" (DiceState at the start of a game) is a fact; only an
    // absent one is missing.
    auto *a=root?root->FindAClass(root,path):nullptr;
    if(!a)return std::nullopt;const char *s=a->GetThisAttr();return std::string(s?s:"");
}
// DiceState.Hero.d<n> (faces) and .d<n>.Mix (marked for the cup) as heroDie<n> and heroMix<n> ("0" or "1"), and
// the combination types of both rows.
void TableDice(ATTRIBUTES *state,std::vector<std::pair<std::string,std::optional<std::string>>> &facts){
    for(int i=1;i<=5;++i){
        const std::string die="Hero.d"+std::to_string(i);
        facts.emplace_back("heroDie"+std::to_string(i),TableAttribute(state,die.c_str()));
        auto marked=TableAttribute(state,(die+".Mix").c_str());
        if(marked)marked=*marked=="0"||marked->empty()?"0":"1";
        facts.emplace_back("heroMix"+std::to_string(i),marked);
    }
    facts.emplace_back("heroType",TableAttribute(state,"Hero.Result.Type"));
    facts.emplace_back("compType",TableAttribute(state,"Comp.Result.Type"));
}
}

void XINTERFACE::MobileCollectTableGame(std::vector<MobileWidget> &widgets)
{
    storm::mobile::TableGameSource source;
    for(auto *p=m_imgLists;p;p=p->next){
        if(!p->sImageName)continue;
        storm::mobile::TableGameImage image{p->sImageName,static_cast<int>(p->position.left),static_cast<int>(p->position.top)};
        if(p->sImageListName&&*p->sImageListName&&p->sPicture&&*p->sPicture){
            image.image=storm::mobile::AtlasReference(pPictureService,p->sImageListName,p->sPicture);
            image.image.source={"atlas",p->sImageListName,p->sPicture,{},{},-1};
        }
        source.images.push_back(std::move(image));
    }
    if(auto *strings=core.Entity_GetAttributeClass(g_idInterface,"strings"))
        for(size_t i=0;i<strings->GetAttributesNum();++i){
            const char *name=strings->GetAttributeName(i);const char *text=strings->GetAttribute(i);
            if(name)source.strings.emplace_back(name,text?text:"");
        }
    auto &facts=source.facts;
    for(const auto &[key,name]:std::initializer_list<std::pair<const char*,const char*>>{
        {"rate","iRate"},{"moneyHero","iMoneyP"},{"bank","iChest"},{"open","openExit"},
        {"stage","bStartGame"},{"turn","dir_i"},{"first","dir_i_start"}})facts.emplace_back(key,TableInteger(name));
    // The opponent's character index: his name and face are the game's (GetFullName, FaceId), read by the adapter.
    facts.emplace_back("opponent",TableAttribute(MobileVariableObject("npchar"),"index"));
    if(storm::android::mobileScreen=="boal_dice"){
        facts.emplace_back("locked",TableInteger("bLockClick"));
        auto *state=MobileVariableObject("DiceState");
        TableDice(state,facts);
    }else facts.emplace_back("cardMove",TableInteger("cardMove"));
    storm::mobile::CollectTableGame(source,widgets);
}
