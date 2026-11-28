// Included inside xinterface.cpp's Android implementation: the pieces of the tavern mini-games
// (PROGRAM/interface/boal_dice.c, boal_cards.c) for mobile_table_game.hpp: the script images and strings, which are
// no nodes. All rules stay in script.
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
    storm::mobile::CollectTableGame(source,widgets);
}
