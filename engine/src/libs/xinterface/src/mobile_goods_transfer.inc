// Included inside xinterface.cpp's Android implementation: the facts of the treasurer's purchase
// list (PROGRAM/interface/GoodsTransfer.c) for mobile_goods_transfer.hpp. All rules stay in script.
namespace {
ATTRIBUTES *GoodsScriptObject(const char *name, std::optional<uint32_t> index = std::nullopt){
    auto *v=static_cast<VDATA*>(core.GetScriptVariable(name));
    return !v?nullptr:index?v->GetAClass(*index):v->GetAClass();
}
std::optional<int> GoodsInteger(const char *name){
    auto *v=static_cast<VDATA*>(core.GetScriptVariable(name));int32_t n;
    return v&&v->Get(n)?std::optional<int>(n):std::nullopt;
}
const char *GoodsAttr(ATTRIBUTES *root,const std::string &path){
    auto *a=root?root->FindAClass(root,path.c_str()):nullptr;return a?a->GetThisAttr():nullptr;
}
}

void XINTERFACE::MobileCollectGoodsTransfer(std::vector<MobileWidget> &widgets)
{
    auto *list=FindNode("MAIN_WINDOW",nullptr);
    auto *amount=FindNode("GOODS_TRANSFER_WINDOW",nullptr);
    const bool amountShown=amount&&amount->m_nNodeType==NODETYPE_WINDOW&&static_cast<CXI_WINDOW*>(amount)->GetShow();
    const bool listActive=list&&list->m_nNodeType==NODETYPE_WINDOW&&static_cast<CXI_WINDOW*>(list)->GetActive()&&!amountShown;
    // The list: the collection's name strings (id = good) that have their "Transfer<good>" partner.
    std::map<std::string,std::string> names;std::vector<std::string> order;std::set<std::string> partners;
    for(const auto &w:widgets)if(w.node=="GOODS_NAMES"&&!w.rowId.empty()){
        if(w.rowId.rfind("Transfer",0)==0)partners.insert(w.rowId.substr(8));
        else if(names.emplace(w.rowId,w.label).second)order.push_back(w.rowId);
    }
    std::vector<storm::mobile::GoodsTransferGood> goods;
    for(const auto &id:order)if(partners.count(id))goods.push_back({id,names[id]});
    storm::mobile::CollectGoodsTransfer(goods,listActive,widgets);
}

bool XINTERFACE::MobileApplyGoodsTransfer(const storm::ui::Action &action)
{
    // Only from the list window, for a good the list shows, at this opening of the screen.
    const auto order=action.verb=="order"?storm::mobile::GoodsTransferOrder(action.value):std::nullopt;
    std::vector<MobileWidget> live;MobileCollectWidgets(live);
    const auto row=std::find_if(live.begin(),live.end(),[&](const auto &w){
        return order&&w.node=="ANDROID_GOODS_ORDER"&&w.rowId==order->good&&w.enabled;});
    if(row==live.end())return false;
    const std::string &id=order->good;
    auto *icons=core.Entity_GetAttributeClass(g_idInterface,"GOODS_ICONS");
    auto *window=GoodsScriptObject("showWindow");
    const char *gx=GoodsAttr(icons,"imagelist."+id+".x"),*gy=GoodsAttr(icons,"imagelist."+id+".y");
    const char *ox=GoodsAttr(icons,"offset.x"),*oy=GoodsAttr(icons,"offset.y");
    const char *sx=GoodsAttr(icons,"scale.x"),*sy=GoodsAttr(icons,"scale.y");
    const char *ww=GoodsAttr(window,"width"),*wh=GoodsAttr(window,"height");
    if(!gx||!gy||!ox||!oy||!sx||!sy||!ww||!wh||std::atof(sx)<=0||std::atof(sy)<=0){
        core.Trace("goodstransfer: cannot order %s: GOODS_ICONS or showWindow geometry missing",id.c_str());
        return false;
    }
    const storm::mobile::GoodsIconPicture picture{static_cast<float>(std::atof(ox)),static_cast<float>(std::atof(oy)),
        static_cast<float>(std::atof(sx)),static_cast<float>(std::atof(sy)),
        storm::mobile::GoodsWindowOffsetX(static_cast<float>(std::atof(ww)),static_cast<float>(std::atof(wh)))};
    const auto point=storm::mobile::GoodsIconClick(static_cast<float>(std::atof(gx)),static_cast<float>(std::atof(gy)),picture);
    ATTRIBUTES *mouse=AttributesPointer->GetAttributeClass("mousepos");
    if(!mouse)mouse=AttributesPointer->CreateSubAClass(AttributesPointer,"mousepos");
    if(!mouse)return false;
    mouse->SetAttributeUseFloat("x",point.x);mouse->SetAttributeUseFloat("y",point.y);
    core.Event("ShowItemsWindow");
    // The script must now show this good's amount window; anything else is not ours to confirm.
    const auto current=GoodsInteger("iCurGoodIndex");
    const char *opened=current&&*current>=0?GoodsAttr(GoodsScriptObject("Goods",static_cast<uint32_t>(*current)),"name"):nullptr;
    auto *edit=FindNode("TG_EDIT",nullptr);auto *ok=FindNode("TG_OK_BUTTON",nullptr);
    if(!opened||id!=opened||!edit||!edit->m_bUse||!ok||!ok->m_bUse){
        core.Trace("goodstransfer: the amount window of %s did not open",id.c_str());
        return false;
    }
    storm::ui::Action amount=action;
    amount.node=edit->m_nodeName;amount.verb="edit";amount.value=std::to_string(order->amount);amount.index=-1;
    if(!edit->MobileApply(amount))return false;
    const int code=ok->CheckCommandUsed(ACTION_MOUSECLICK)?ACTION_MOUSECLICK:ACTION_ACTIVATE;
    bool stop=false;if(auto *next=ok->DoAction(code,stop,true))SetCurNode(next);
    return true;
}
