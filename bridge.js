// Laeuft im Panorama-Kontext des Hauptmenues (eingebettet in die DLL, siehe build.sh).
// Meldet den Menuezustand per $.Msg in die Konsolen-Logdatei. Format (Werte URL-kodiert):
//   RPC|<gen>|hb                                       Heartbeat
//   RPC|<gen>|tab=..|mode=..|name=..|type=..|wear=..|float=..|recent=0/1   bei Aenderung
// mode: none | inspect | crate | opening
(function () {
    if (typeof __rpcGen === 'undefined') __rpcGen = 0;
    var gen = ++__rpcGen;
    var last = '', ticks = 0;
    var E = encodeURIComponent;

    function root() {
        var p = $.GetContextPanel();
        while (p && p.GetParent && p.GetParent()) p = p.GetParent();
        return p;
    }
    function T(f) { try { var v = f(); return (v === undefined || v === null) ? '' : String(v); } catch (e) { return ''; } }
    function tabOf(r) {
        var c = r.FindChildTraverse('JsMainMenuContent');
        if (!c || c.BHasClass('mainmenu-content--offscreen')) return '';
        var kids = c.Children(), t = '';
        for (var i = 0; i < kids.length; i++) if (!kids[i].BHasClass('mainmenu-content--hidden') && kids[i].visible && !t) t = kids[i].id;
        return t;
    }
    // Die Reihenfolge der Kinder im PopupManager ist keine Stapelreihenfolge; "oben" ist das zuletzt neu erschienene
    // Popup vom Typ Kiste ("dec") oder Item-Inspect ("ins").
    var order = [];
    function topPopup(r) {
        var pm = r.FindChildTraverse('PopupManager');
        var cur = {};
        if (pm) {
            var k = pm.Children();
            for (var i = 0; i < k.length; i++) {
                if (k[i].BHasClass('hidden')) continue;
                var lf = T(function () { return k[i].layoutfile; });
                var kind = lf.indexOf('popup_capability_decodable') >= 0 ? 'dec' : (lf.indexOf('popup_inventory_inspect') >= 0 ? 'ins' : '');
                if (!kind) continue;
                var key = kind + ':' + T(function () { return k[i].GetAttributeString(kind === 'dec' ? 'key-and-case' : 'itemid', ''); });
                cur[key] = { kind: kind, p: k[i] };
            }
        }
        order = order.filter(function (x) { return cur[x]; });
        for (var key2 in cur) if (order.indexOf(key2) < 0) order.push(key2);
        return order.length ? cur[order[order.length - 1]] : null;
    }
    function itemInfo(id) {
        var wear = T(function () { return InventoryAPI.GetWear(id); });
        var fl = T(function () { return InventoryAPI.GetItemAttributeValue(id, 'set item texture wear'); });
        return 'name=' + E(T(function () { return InventoryAPI.GetItemName(id); })) +
            '|type=' + E(T(function () { return InventoryAPI.GetItemType(id); })) +
            '|wear=' + E(wear !== '' && parseInt(wear, 10) >= 0 ? T(function () { return $.Localize('#SFUI_InvTooltip_Wear_Amount_' + wear); }) : '') +
            '|float=' + E(fl !== '' && wear !== '' && parseInt(wear, 10) >= 0 ? parseFloat(fl).toFixed(6) : '');
    }
    function state(r) {
        var tab = 'tab=' + E(tabOf(r));
        var top = topPopup(r);
        if (top && top.kind === 'dec') {
            var dec = top.p;
            var kc = T(function () { return dec.GetAttributeString('key-and-case', ''); }).split(',');
            var caseId = kc.length > 1 ? kc[1] : kc[0];
            var scroll = dec.FindChildTraverse('DecodableItemsScroll');
            var opening = scroll && !scroll.BHasClass('hidden');
            return tab + '|mode=' + (opening ? 'opening' : 'crate') + '|' + itemInfo(caseId) + '|recent=0';
        }
        if (top && top.kind === 'ins') {
            var ins = top.p;
            var id = T(function () { return ins.GetAttributeString('itemid', ''); });
            if (id) {
                var recent = T(function () { return InventoryAPI.GetItemSessionPropertyValue(id, 'recent'); }) === '1' ? 1 : 0;
                return tab + '|mode=inspect|' + itemInfo(id) + '|recent=' + recent;
            }
        }
        return tab + '|mode=none';
    }
    function poll() {
        if (gen !== __rpcGen) return;
        try {
            var s = state(root());
            if (s !== last) { last = s; $.Msg('RPC|' + gen + '|' + s); }
            else if (++ticks % 3 === 0) $.Msg('RPC|' + gen + '|hb');
        } catch (e) {
            // Fehler (z.B. Panels noch nicht bereit) ignorieren, naechster Tick versucht es erneut
        }
        $.Schedule(1.0, poll);
    }
    GameInterfaceAPI.ConsoleCommand('con_logfile rpc_console.log');
    poll();
})();
