// Included inside xinterface.cpp's Android implementation: the recipes of the alchemy screen
// (PROGRAM/interface/alchemy.c) for mobile_alchemy.hpp, and choosing one as a tap on the scroll does.
namespace {
std::optional<int> AlchemyInteger(const char *name){
    const auto n=MobileVariableInteger(name);
    return n?std::optional<int>(*n):std::nullopt;
}
std::string AlchemyString(const char *name){
    return MobileVariableText(name).value_or("");
}
const char *AlchemyAttr(ATTRIBUTES *root,const std::string &path){
    auto *a=root?root->FindAClass(root,path.c_str()):nullptr;return a?a->GetThisAttr():nullptr;
}
// Items index of every item id the screen names (one pass over the script's Items array).
std::map<std::string,uint32_t> AlchemyItemIndex(const std::set<std::string> &ids){
    std::map<std::string,uint32_t> out;
    auto *items=static_cast<VDATA*>(core.GetScriptVariable("Items"));
    if(!items)return out;
    for(uint32_t i=0;i<items->GetElementsNum()&&out.size()<ids.size();++i){
        const char *id=AlchemyAttr(items->GetAClass(i),"id");
        if(id&&ids.count(id))out.emplace(id,i);
    }
    return out;
}
}

storm::mobile::AlchemySource XINTERFACE::MobileAlchemySource()
{
    storm::mobile::AlchemySource source;
    auto *root=AttributesPointer;
    auto *list=root?root->GetAttributeClass("ITEMS_SCROLL"):nullptr;
    auto *scroll=FindNode("ITEMS_SCROLL",nullptr);
    auto *items=static_cast<VDATA*>(core.GetScriptVariable("Items"));
    source.current=AlchemyString("sCurItem");
    if(!list)source.missing.push_back("ITEMS_SCROLL");
    if(!scroll||scroll->m_nNodeType!=NODETYPE_SCROLLIMAGE)source.missing.push_back("ITEMS_SCROLL node");
    if(!items)source.missing.push_back("Items");
    if(source.current.empty())source.missing.push_back("sCurItem");
    if(!source.missing.empty())return source;

    // The recipes: only picN records with an item (ListSize and NotUsed count padding).
    std::vector<std::pair<int,std::string>> pictures;std::set<std::string> ids{source.current};
    for(uint32_t i=0;i<list->GetAttributesNum();++i){
        auto *entry=list->GetAttributeClass(i);const std::string name=entry?entry->GetThisName():"";
        if(name.rfind("pic",0)!=0||name.size()<4||name.find_first_not_of("0123456789",3)!=std::string::npos)continue;
        const char *id=AlchemyAttr(entry,"itemId");
        if(!id||!*id)continue;
        pictures.emplace_back(std::atoi(name.c_str()+3)-1,id);ids.insert(id);
    }
    std::sort(pictures.begin(),pictures.end());
    auto index=AlchemyItemIndex(ids);
    if(m_mobileAlchemyStrings<0||m_mobileAlchemyLanguage!=(pStringService->GetLanguage()?pStringService->GetLanguage():"")){
        if(m_mobileAlchemyStrings>=0)pStringService->CloseUsersStringFile(m_mobileAlchemyStrings);
        m_mobileAlchemyStrings=pStringService->OpenUsersStringFile("ItemsDescribe.txt");
        m_mobileAlchemyLanguage=pStringService->GetLanguage()?pStringService->GetLanguage():"";
    }
    const auto text=[&](const char *key)->std::string{
        if(!key||!*key||m_mobileAlchemyStrings<0)return {};
        const char *s=pStringService->TranslateFromUsers(m_mobileAlchemyStrings,key);return s?s:"";
    };
    for(const auto &[position,id]:pictures){
        const auto found=index.find(id);
        if(found==index.end()){source.missing.push_back("item:"+id);continue;}
        auto *item=items->GetAClass(found->second);
        storm::mobile::AlchemyRecipe recipe;recipe.id=id;recipe.index=position;
        recipe.name=text(AlchemyAttr(item,"name"));
        if(recipe.name.empty())source.missing.push_back("name:"+id);
        const char *tex=AlchemyAttr(item,"picTexture"),*pic=AlchemyAttr(item,"picIndex");
        recipe.atlasGroup=tex?tex:"";recipe.atlasImage=pic?std::string("itm")+pic:"";
        source.recipes.push_back(std::move(recipe));
    }
    if(source.recipes.empty())source.missing.push_back("recipes");

    // Ready only when the script shows what the scroll shows and nothing is in flight.
    auto *images=static_cast<CXI_SCROLLIMAGE*>(scroll);
    const auto seen=AlchemyInteger("nCurScrollNum"),window=AlchemyInteger("bShowChangeWin");
    const char *atScroll=AlchemyAttr(list,"pic"+std::to_string(images->MobileCurrent()+1)+".itemId");
    auto *tooltip=FindNode("tooltip_fader",nullptr);
    if(!m_bUse||!bActive)source.busy="inactive";
    else if(!m_events.Empty())source.busy="events";
    else if(images->MobileMoving())source.busy="scrolling";
    else if(!seen||*seen!=images->MobileCurrent()||!atScroll||source.current!=atScroll)source.busy="recipe-pending";
    else if(!window||*window!=0)source.busy="amount-window";
    else if(tooltip&&tooltip->m_bUse)source.busy="tooltip";
    return source;
}

void XINTERFACE::MobileCollectAlchemy(std::vector<MobileWidget> &widgets)
{
    const auto source=MobileAlchemySource();
    // What the script lacks leaves no recipe to choose: said once per change, not every model.
    std::string missing;
    for(const auto &fact:source.missing)missing+=(missing.empty()?"":",")+fact;
    if(missing!=m_mobileAlchemyMissing&&!missing.empty())core.Trace("alchemy: the script lacks %s",missing.c_str());
    m_mobileAlchemyMissing=missing;
    storm::mobile::CollectAlchemy(source,widgets);
}

bool XINTERFACE::MobileApplyAlchemy(const storm::ui::Action &action)
{
    const auto source=MobileAlchemySource();
    if(!source.busy.empty()||!source.missing.empty()){core.Trace("alchemy: %s refused: %s",action.value.c_str(),source.busy.c_str());return false;}
    const std::string item=action.verb=="choose"?action.value:std::string();
    const auto recipe=std::find_if(source.recipes.begin(),source.recipes.end(),[&](const auto &r){return r.id==item;});
    if(item.empty()||recipe==source.recipes.end()||recipe->id==source.current)return false;
    static_cast<CXI_SCROLLIMAGE*>(FindNode("ITEMS_SCROLL",nullptr))->MobileSelect(recipe->index);
    return true;
}
