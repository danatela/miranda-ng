/*

Miranda NG: the free IM client for Microsoft* Windows*

Copyright (�) 2012-16 Miranda NG project (http://miranda-ng.org),
Copyright (c) 2000-12 Miranda IM project,
all portions of this codebase are copyrighted to the people
listed in contributors.txt.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "stdafx.h"

#include "IcoLib.h"

struct TreeItem
{
	char *paramName;
	DWORD value;
};

/////////////////////////////////////////////////////////////////////////////////////////

static HICON ExtractIconFromPath(const wchar_t *path, int cxIcon, int cyIcon)
{
	wchar_t *comma;
	wchar_t file[MAX_PATH], fileFull[MAX_PATH];
	int n;
	HICON hIcon;

	if (!path)
		return (HICON)NULL;

	mir_wstrncpy(file, path, _countof(file));
	comma = wcsrchr(file, ',');
	if (!comma)
		n = 0;
	else {
		n = _wtoi(comma + 1);
		*comma = 0;
	}
	PathToAbsoluteW(file, fileFull);
	hIcon = NULL;

	//SHOULD BE REPLACED WITH GOOD ENOUGH FUNCTION
	_ExtractIconEx(fileFull, n, cxIcon, cyIcon, &hIcon, LR_COLOR);
	return hIcon;
}

/////////////////////////////////////////////////////////////////////////////////////////
// IconItem_GetIcon_Preview

HICON IconItem_GetIcon_Preview(IcolibItem* item)
{
	HICON hIcon = NULL;

	if (!item->temp_reset) {
		HICON hRefIcon = IcoLib_GetIconByHandle((HANDLE)item, false);
		hIcon = CopyIcon(hRefIcon);
		if (item->source_small && item->source_small->icon == hRefIcon)
			item->source_small->releaseIcon();
	}
	else {
		if (item->default_icon) {
			HICON hRefIcon = item->default_icon->getIcon();
			if (hRefIcon) {
				hIcon = CopyIcon(hRefIcon);
				if (item->default_icon->icon == hRefIcon)
					item->default_icon->releaseIcon();
			}
		}

		if (!hIcon && item->default_file) {
			item->default_icon->release();
			item->default_icon = GetIconSourceItem(item->default_file->file, item->default_indx, item->cx, item->cy);
			if (item->default_icon) {
				HICON hRefIcon = item->default_icon->getIcon();
				if (hRefIcon) {
					hIcon = CopyIcon(hRefIcon);
					if (item->default_icon->icon == hRefIcon)
						item->default_icon->releaseIcon();
				}
			}
		}

		if (!hIcon)
			return CopyIcon(hIconBlank);
	}
	return hIcon;
}

//  User interface

#define DM_REBUILDICONSPREVIEW  (WM_USER+10)
#define DM_CHANGEICON           (WM_USER+11)
#define DM_CHANGESPECIFICICON   (WM_USER+12)
#define DM_UPDATEICONSPREVIEW   (WM_USER+13)

static void __fastcall MySetCursor(wchar_t *nCursor)
{
	SetCursor(LoadCursor(NULL, nCursor));
}

static wchar_t* OpenFileDlg(HWND hParent, const wchar_t *szFile, BOOL bAll)
{
	OPENFILENAME ofn = { 0 };
	wchar_t filter[512], *pfilter, file[MAX_PATH * 2];

	ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400;
	ofn.hwndOwner = hParent;

	mir_wstrcpy(filter, TranslateT("Icon sets"));
	if (bAll)
		mir_wstrcat(filter, L" (*.dll;*.mir;*.icl;*.exe;*.ico)");
	else
		mir_wstrcat(filter, L" (*.dll;*.mir)");

	pfilter = filter + mir_wstrlen(filter) + 1;
	if (bAll)
		mir_wstrcpy(pfilter, L"*.DLL;*.MIR;*.ICL;*.EXE;*.ICO");
	else
		mir_wstrcpy(pfilter, L"*.DLL;*.MIR");

	pfilter += mir_wstrlen(pfilter) + 1;
	mir_wstrcpy(pfilter, TranslateT("All files"));
	mir_wstrcat(pfilter, L" (*)");
	pfilter += mir_wstrlen(pfilter) + 1;
	mir_wstrcpy(pfilter, L"*");
	pfilter += mir_wstrlen(pfilter) + 1;
	*pfilter = '\0';

	ofn.lpstrFilter = filter;
	ofn.lpstrDefExt = L"dll";
	mir_wstrncpy(file, szFile, _countof(file));
	ofn.lpstrFile = file;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_DONTADDTORECENT;
	ofn.nMaxFile = MAX_PATH * 2;

	if (!GetOpenFileName(&ofn))
		return NULL;

	return mir_wstrdup(file);
}

/////////////////////////////////////////////////////////////////////////////////////////
// icon import dialog's window procedure

class CIconImportDlg : public CDlgBase
{
	HWND m_hwndParent, m_hwndDragOver;
	int  m_iDragItem, m_iDropHiLite;
	bool m_bDragging;

	CCtrlListView m_preview;
	CCtrlButton m_btnGetMore, m_btnBrowse;
	CCtrlEdit m_iconSet;

public:
	CIconImportDlg(HWND _parent) :
		CDlgBase(g_hInst, IDD_ICOLIB_IMPORT),
		m_preview(this, IDC_PREVIEW),
		m_iconSet(this, IDC_ICONSET),
		m_btnBrowse(this, IDC_BROWSE),
		m_btnGetMore(this, IDC_GETMORE),
		m_hwndParent(_parent),
		m_bDragging(false),
		m_iDragItem(0),
		m_iDropHiLite(0)
	{
		m_btnBrowse.OnClick = Callback(this, &CIconImportDlg::OnBrowseClick);
		m_btnGetMore.OnClick = Callback(this, &CIconImportDlg::OnGetMoreClick);
		m_iconSet.OnChange = Callback(this, &CIconImportDlg::OnEditChange);
		m_preview.OnBeginDrag = Callback(this, &CIconImportDlg::OnBeginDragPreview);
	}

	virtual void OnInitDialog() override
	{
		m_preview.SetImageList(ImageList_Create(g_iIconSX, g_iIconSY, ILC_COLOR32 | ILC_MASK, 0, 100), LVSIL_NORMAL);
		m_preview.SetIconSpacing(56, 67);

		RECT rcThis, rcParent;
		int cxScreen = GetSystemMetrics(SM_CXSCREEN);

		GetWindowRect(m_hwnd, &rcThis);
		GetWindowRect(m_hwndParent, &rcParent);
		OffsetRect(&rcThis, rcParent.right - rcThis.left, 0);
		OffsetRect(&rcThis, 0, rcParent.top - rcThis.top);
		GetWindowRect(GetParent(m_hwndParent), &rcParent);
		if (rcThis.right > cxScreen) {
			OffsetRect(&rcParent, cxScreen - rcThis.right, 0);
			OffsetRect(&rcThis, cxScreen - rcThis.right, 0);
			MoveWindow(GetParent(m_hwndParent), rcParent.left, rcParent.top, rcParent.right - rcParent.left, rcParent.bottom - rcParent.top, TRUE);
		}
		MoveWindow(m_hwnd, rcThis.left, rcThis.top, rcThis.right - rcThis.left, rcThis.bottom - rcThis.top, FALSE);
		GetClientRect(m_hwnd, &rcThis);
		SendMessage(m_hwnd, WM_SIZE, 0, MAKELPARAM(rcThis.right - rcThis.left, rcThis.bottom - rcThis.top));

		SHAutoComplete(m_iconSet.GetHwnd(), 1);
		m_iconSet.SetText(L"icons.dll");
	}

	virtual void OnClose() override
	{
		EnableWindow(GetDlgItem(m_hwndParent, IDC_IMPORT), TRUE);
	}

	virtual int Resizer(UTILRESIZECONTROL *urc) override
	{
		switch (urc->wId) {
		case IDC_ICONSET:
			return RD_ANCHORX_WIDTH | RD_ANCHORY_TOP;

		case IDC_BROWSE:
			return RD_ANCHORX_RIGHT | RD_ANCHORY_TOP;

		case IDC_PREVIEW:
			return RD_ANCHORX_WIDTH | RD_ANCHORY_HEIGHT;

		case IDC_GETMORE:
			return RD_ANCHORX_CENTRE | RD_ANCHORY_BOTTOM;
		}
		return RD_ANCHORX_LEFT | RD_ANCHORY_TOP; // default
	}

	void OnEditChange(void*)
	{	RebuildIconsPreview();
	}

	void OnGetMoreClick(void*)
	{	Utils_OpenUrl("http://miranda-ng.org/");
	}

	void OnBrowseClick(void*)
	{
		wchar_t str[MAX_PATH];
		GetDlgItemText(m_hwnd, IDC_ICONSET, str, _countof(str));
		if (wchar_t *file = OpenFileDlg(GetParent(m_hwnd), str, TRUE)) {
			m_iconSet.SetText(file);
			mir_free(file);
		}
	}

	void OnBeginDragPreview(CCtrlListView::TEventInfo *evt)
	{
		SetCapture(m_hwnd);
		m_bDragging = true;
		m_iDragItem = evt->nmlv->iItem;
		m_iDropHiLite = -1;
		ImageList_BeginDrag(m_preview.GetImageList(LVSIL_NORMAL), m_iDragItem, g_iIconX / 2, g_iIconY / 2);

		POINT pt;
		GetCursorPos(&pt);

		RECT rc;
		GetWindowRect(m_hwnd, &rc);
		ImageList_DragEnter(m_hwnd, pt.x - rc.left, pt.y - rc.top);
		m_hwndDragOver = m_hwnd;
	}

	void RebuildIconsPreview()
	{
		MySetCursor(IDC_WAIT);
		m_preview.DeleteAllItems();
		HIMAGELIST hIml = m_preview.GetImageList(LVSIL_NORMAL);
		ImageList_RemoveAll(hIml);

		wchar_t filename[MAX_PATH], caption[64];
		m_iconSet.GetText(filename, _countof(filename));

		RECT rcPreview, rcGroup;
		GetWindowRect(m_preview.GetHwnd(), &rcPreview);
		GetWindowRect(GetDlgItem(m_hwnd, IDC_IMPORTMULTI), &rcGroup);
		// SetWindowPos(hPreview, 0, 0, 0, rcPreview.right-rcPreview.left, rcGroup.bottom-rcPreview.top, SWP_NOZORDER|SWP_NOMOVE);

		if (_waccess(filename, 0) != 0) {
			MySetCursor(IDC_ARROW);
			return;
		}

		LVITEM lvi;
		lvi.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
		lvi.iSubItem = 0;
		lvi.iItem = 0;
		int count = (int)_ExtractIconEx(filename, -1, 16, 16, NULL, LR_DEFAULTCOLOR);
		for (int i = 0; i < count; lvi.iItem++, i++) {
			mir_snwprintf(caption, L"%d", i + 1);
			lvi.pszText = caption;

			HICON hIcon = NULL;
			if (_ExtractIconEx(filename, i, 16, 16, &hIcon, LR_DEFAULTCOLOR) == 1) {
				lvi.iImage = ImageList_AddIcon(hIml, hIcon);
				DestroyIcon(hIcon);
				lvi.lParam = i;
				m_preview.InsertItem(&lvi);
			}
		}
		MySetCursor(IDC_ARROW);
	}

	virtual INT_PTR DlgProc(UINT msg, WPARAM wParam, LPARAM lParam) override
	{
		switch (msg) {
		case WM_MOUSEMOVE:
			if (m_bDragging) {
				LVHITTESTINFO lvhti;
				int onItem = 0;
				HWND hwndOver;
				POINT ptDrag;
				HWND hPPreview = GetDlgItem(m_hwndParent, IDC_PREVIEW);

				lvhti.pt.x = (short)LOWORD(lParam); lvhti.pt.y = (short)HIWORD(lParam);
				ClientToScreen(m_hwnd, &lvhti.pt);
				hwndOver = WindowFromPoint(lvhti.pt);

				RECT rc;
				GetWindowRect(hwndOver, &rc);
				ptDrag.x = lvhti.pt.x - rc.left; ptDrag.y = lvhti.pt.y - rc.top;
				if (hwndOver != m_hwndDragOver) {
					ImageList_DragLeave(m_hwndDragOver);
					m_hwndDragOver = hwndOver;
					ImageList_DragEnter(m_hwndDragOver, ptDrag.x, ptDrag.y);
				}

				ImageList_DragMove(ptDrag.x, ptDrag.y);
				if (hwndOver == hPPreview) {
					ScreenToClient(hPPreview, &lvhti.pt);

					if (ListView_HitTest(hPPreview, &lvhti) != -1) {
						if (lvhti.iItem != m_iDropHiLite) {
							ImageList_DragLeave(m_hwndDragOver);
							if (m_iDropHiLite != -1)
								ListView_SetItemState(hPPreview, m_iDropHiLite, 0, LVIS_DROPHILITED);
							m_iDropHiLite = lvhti.iItem;
							ListView_SetItemState(hPPreview, m_iDropHiLite, LVIS_DROPHILITED, LVIS_DROPHILITED);
							UpdateWindow(hPPreview);
							ImageList_DragEnter(m_hwndDragOver, ptDrag.x, ptDrag.y);
						}
						onItem = 1;
					}
				}

				if (!onItem && m_iDropHiLite != -1) {
					ImageList_DragLeave(m_hwndDragOver);
					ListView_SetItemState(hPPreview, m_iDropHiLite, 0, LVIS_DROPHILITED);
					UpdateWindow(hPPreview);
					ImageList_DragEnter(m_hwndDragOver, ptDrag.x, ptDrag.y);
					m_iDropHiLite = -1;
				}
				MySetCursor(onItem ? IDC_ARROW : IDC_NO);
			}
			break;

		case WM_LBUTTONUP:
			if (m_bDragging) {
				ReleaseCapture();
				ImageList_EndDrag();
				m_bDragging = 0;
				if (m_iDropHiLite != -1) {
					wchar_t fullPath[MAX_PATH], filename[MAX_PATH];
					m_iconSet.GetText(fullPath, _countof(fullPath));
					PathToRelativeT(fullPath, filename);

					LVITEM lvi;
					lvi.mask = LVIF_PARAM;
					lvi.iItem = m_iDragItem; lvi.iSubItem = 0;
					m_preview.GetItem(&lvi);

					SendMessage(m_hwndParent, DM_CHANGEICON, m_iDropHiLite, (LPARAM)CMStringW(FORMAT, L"%s,%d", filename, (int)lvi.lParam).c_str());
					ListView_SetItemState(GetDlgItem(m_hwndParent, IDC_PREVIEW), m_iDropHiLite, 0, LVIS_DROPHILITED);
				}
			}
			break;
		}

		return CDlgBase::DlgProc(msg, wParam, lParam);
	}
};

/////////////////////////////////////////////////////////////////////////////////////////
// IcoLib options window procedure

static int CALLBACK DoSortIconsFunc(LPARAM lParam1, LPARAM lParam2, LPARAM)
{
	return mir_wstrcmpi(iconList[lParam1]->getDescr(), iconList[lParam2]->getDescr());
}

static int CALLBACK DoSortIconsFuncByOrder(LPARAM lParam1, LPARAM lParam2, LPARAM)
{
	return iconList[lParam1]->orderID - iconList[lParam2]->orderID;
}

static int OpenPopupMenu(HWND hwndDlg)
{
	POINT pt;
	GetCursorPos(&pt);
	HMENU hMenu = LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_ICOLIB_CONTEXT));
	HMENU hPopup = GetSubMenu(hMenu, 0);
	TranslateMenu(hPopup);
	int cmd = TrackPopupMenu(hPopup, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwndDlg, NULL);
	DestroyMenu(hMenu);
	return cmd;
}

static void LoadSectionIcons(wchar_t *filename, SectionItem* sectionActive)
{
	wchar_t path[MAX_PATH];
	mir_snwprintf(path, L"%s,", filename);
	size_t suffIndx = mir_wstrlen(path);

	mir_cslock lck(csIconList);

	for (int indx = 0; indx < iconList.getCount(); indx++) {
		IcolibItem *item = iconList[indx];

		if (item->default_file && item->section == sectionActive) {
			_itow(item->default_indx, path + suffIndx, 10);
			HICON hIcon = ExtractIconFromPath(path, item->cx, item->cy);
			if (!hIcon)
				continue;

			replaceStrW(item->temp_file, NULL);
			SafeDestroyIcon(item->temp_icon);

			item->temp_file = mir_wstrdup(path);
			item->temp_icon = hIcon;
			item->temp_reset = FALSE;
		}
	}
}

static void LoadSubIcons(HWND htv, wchar_t *filename, HTREEITEM hItem)
{
	TVITEM tvi;
	tvi.mask = TVIF_HANDLE | TVIF_PARAM;
	tvi.hItem = hItem;
	TreeView_GetItem(htv, &tvi);

	TreeItem *treeItem = (TreeItem *)tvi.lParam;
	SectionItem* sectionActive = sectionList[SECTIONPARAM_INDEX(treeItem->value)];

	tvi.hItem = TreeView_GetChild(htv, tvi.hItem);
	while (tvi.hItem) {
		LoadSubIcons(htv, filename, tvi.hItem);
		tvi.hItem = TreeView_GetNextSibling(htv, tvi.hItem);
	}

	if (SECTIONPARAM_FLAGS(treeItem->value) & SECTIONPARAM_HAVEPAGE)
		LoadSectionIcons(filename, sectionActive);
}

static void UndoChanges(int iconIndx, int cmd)
{
	IcolibItem *item = iconList[iconIndx];

	if (!item->temp_file && !item->temp_icon && item->temp_reset && cmd == ID_CANCELCHANGE)
		item->temp_reset = FALSE;
	else {
		replaceStrW(item->temp_file, NULL);
		SafeDestroyIcon(item->temp_icon);
	}

	if (cmd == ID_RESET)
		item->temp_reset = TRUE;
}

static void UndoSubItemChanges(HWND htv, HTREEITEM hItem, int cmd)
{
	TVITEM tvi = { 0 };
	tvi.mask = TVIF_HANDLE | TVIF_PARAM;
	tvi.hItem = hItem;
	TreeView_GetItem(htv, &tvi);

	TreeItem *treeItem = (TreeItem *)tvi.lParam;
	if (SECTIONPARAM_FLAGS(treeItem->value) & SECTIONPARAM_HAVEPAGE) {
		mir_cslock lck(csIconList);

		for (int indx = 0; indx < iconList.getCount(); indx++)
			if (iconList[indx]->section == sectionList[SECTIONPARAM_INDEX(treeItem->value)])
				UndoChanges(indx, cmd);
	}

	tvi.hItem = TreeView_GetChild(htv, tvi.hItem);
	while (tvi.hItem) {
		UndoSubItemChanges(htv, tvi.hItem, cmd);
		tvi.hItem = TreeView_GetNextSibling(htv, tvi.hItem);
	}
}

static void DoOptionsChanged(HWND hwndDlg)
{
	SendMessage(hwndDlg, DM_UPDATEICONSPREVIEW, 0, 0);
	SendMessage(GetParent(hwndDlg), PSM_CHANGED, 0, 0);
}

static void DoIconsChanged(HWND hwndDlg)
{
	SendMessage(hwndDlg, DM_UPDATEICONSPREVIEW, 0, 0);

	iconEventActive = 1; // Disable icon destroying - performance boost
	NotifyEventHooks(hIconsChangedEvent, 0, 0);
	NotifyEventHooks(hIcons2ChangedEvent, 0, 0);
	iconEventActive = 0;

	mir_cslock lck(csIconList); // Destroy unused icons
	for (int indx = 0; indx < iconList.getCount(); indx++) {
		IcolibItem *item = iconList[indx];
		if (item->source_small && !item->source_small->icon_ref_count) {
			item->source_small->icon_ref_count++;
			item->source_small->releaseIcon();
		}
		if (item->source_big && !item->source_big->icon_ref_count) {
			item->source_big->icon_ref_count++;
			item->source_big->releaseIcon();
		}
	}
}

static HTREEITEM FindNamedTreeItemAt(HWND hwndTree, HTREEITEM hItem, const wchar_t *name)
{
	TVITEM tvi = { 0 };
	wchar_t str[MAX_PATH];

	if (hItem)
		tvi.hItem = TreeView_GetChild(hwndTree, hItem);
	else
		tvi.hItem = TreeView_GetRoot(hwndTree);

	if (!name)
		return tvi.hItem;

	tvi.mask = TVIF_TEXT;
	tvi.pszText = str;
	tvi.cchTextMax = _countof(str);

	while (tvi.hItem) {
		TreeView_GetItem(hwndTree, &tvi);

		if (!mir_wstrcmp(tvi.pszText, name))
			return tvi.hItem;

		tvi.hItem = TreeView_GetNextSibling(hwndTree, tvi.hItem);
	}
	return NULL;
}

/////////////////////////////////////////////////////////////////////////////////////////

class CIcoLibOptsDlg : public CDlgBase
{
	CIconImportDlg *m_pDialog;
	HTREEITEM m_hPrevItem;

	CCtrlTreeView m_categoryList;
	CCtrlListView m_preview;
	CCtrlButton m_btnGetMore, m_btnImport, m_btnLoadIcons;

	void RebuildTree()
	{
		if (!m_categoryList.GetHwnd())
			return;

		m_categoryList.SelectItem(NULL);
		m_categoryList.DeleteAllItems();

		for (int indx = 0; indx < sectionList.getCount(); indx++) {
			int sectionLevel = 0;

			HTREEITEM hSection = NULL;
			wchar_t itemName[1024];
			mir_wstrcpy(itemName, sectionList[indx]->name);
			wchar_t *sectionName = itemName;

			while (sectionName) {
				// allow multi-level tree
				wchar_t *pItemName = sectionName;
				HTREEITEM hItem;

				if (sectionName = wcschr(sectionName, '/')) {
					// one level deeper
					*sectionName = 0;
				}

				pItemName = TranslateW(pItemName);
				hItem = FindNamedTreeItemAt(m_categoryList.GetHwnd(), hSection, pItemName);
				if (!sectionName || !hItem) {
					if (!hItem) {
						TVINSERTSTRUCT tvis = { 0 };
						TreeItem *treeItem = (TreeItem *)mir_alloc(sizeof(TreeItem));
						treeItem->value = SECTIONPARAM_MAKE(indx, sectionLevel, sectionName ? 0 : SECTIONPARAM_HAVEPAGE);
						treeItem->paramName = mir_u2a(itemName);

						tvis.hParent = hSection;
						tvis.hInsertAfter = TVI_SORT;
						tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE;
						tvis.item.pszText = pItemName;
						tvis.item.lParam = (LPARAM)treeItem;
						tvis.item.state = tvis.item.stateMask = db_get_b(NULL, "SkinIconsUI", treeItem->paramName, TVIS_EXPANDED);
						hItem = m_categoryList.InsertItem(&tvis);
					}
					else {
						TVITEMEX tvi = { 0 };
						tvi.hItem = hItem;
						tvi.mask = TVIF_HANDLE | TVIF_PARAM;
						m_categoryList.GetItem(&tvi);
						TreeItem *treeItem = (TreeItem *)tvi.lParam;
						treeItem->value = SECTIONPARAM_MAKE(indx, sectionLevel, SECTIONPARAM_HAVEPAGE);
					}
				}

				if (sectionName) {
					*sectionName = '/';
					sectionName++;
				}
				sectionLevel++;

				hSection = hItem;
			}
		}

		ShowWindow(m_categoryList.GetHwnd(), SW_SHOW);

		m_categoryList.SelectItem(FindNamedTreeItemAt(m_categoryList.GetHwnd(), 0, NULL));
	}

public:
	CIcoLibOptsDlg() :
		CDlgBase(g_hInst, IDD_OPT_ICOLIB),
		m_preview(this, IDC_PREVIEW),
		m_btnImport(this, IDC_IMPORT),
		m_btnGetMore(this, IDC_GETMORE),
		m_btnLoadIcons(this, IDC_LOADICONS),
		m_categoryList(this, IDC_CATEGORYLIST),
		m_hPrevItem(NULL),
		m_pDialog(NULL)
	{
		m_btnImport.OnClick = Callback(this, &CIcoLibOptsDlg::OnImport);
		m_btnGetMore.OnClick = Callback(this, &CIcoLibOptsDlg::OnGetMore);
		m_btnLoadIcons.OnClick = Callback(this, &CIcoLibOptsDlg::OnLoadIcons);

		m_preview.OnGetInfoTip = Callback(this, &CIcoLibOptsDlg::OnGetInfoTip);
		m_categoryList.OnSelChanged = Callback(this, &CIcoLibOptsDlg::OnCategoryChanged);
		m_categoryList.OnDeleteItem = Callback(this, &CIcoLibOptsDlg::OnCategoryDeleted);
	}

	virtual void OnInitDialog() override
	{
		// Reset temporary data & upload sections list
		{
			mir_cslock lck(csIconList);

			for (int indx = 0; indx < iconList.getCount(); indx++) {
				iconList[indx]->temp_file = NULL;
				iconList[indx]->temp_icon = NULL;
				iconList[indx]->temp_reset = false;
			}
			bNeedRebuild = false;
		}

		// Setup preview listview
		m_preview.SetUnicodeFormat(TRUE);
		m_preview.SetExtendedListViewStyleEx(LVS_EX_INFOTIP, LVS_EX_INFOTIP);
		m_preview.SetImageList(ImageList_Create(g_iIconSX, g_iIconSY, ILC_COLOR32 | ILC_MASK, 0, 30), LVSIL_NORMAL);
		m_preview.SetIconSpacing(56, 67);

		RebuildTree();
	}

	virtual INT_PTR DlgProc(UINT msg, WPARAM wParam, LPARAM lParam) override
	{
		switch (msg) {
		case DM_REBUILDICONSPREVIEW: // Rebuild preview to new section
			{
				SectionItem* sectionActive = (SectionItem*)lParam;
				m_preview.Enable(sectionActive != NULL);

				m_preview.DeleteAllItems();
				HIMAGELIST hIml = m_preview.GetImageList(LVSIL_NORMAL);
				ImageList_RemoveAll(hIml);

				if (sectionActive == NULL)
					break;

				LVITEM lvi = { 0 };
				lvi.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
				{
					mir_cslock lck(csIconList);

					for (int indx = 0; indx < iconList.getCount(); indx++) {
						IcolibItem *item = iconList[indx];
						if (item->section == sectionActive) {
							lvi.pszText = item->getDescr();
							HICON hIcon = item->temp_icon;
							if (!hIcon)
								hIcon = IconItem_GetIcon_Preview(item);
							lvi.iImage = ImageList_AddIcon(hIml, hIcon);
							lvi.lParam = indx;
							m_preview.InsertItem(&lvi);
							if (hIcon != item->temp_icon)
								SafeDestroyIcon(hIcon);
						}
					}
				}

				if (sectionActive->flags & SIDF_SORTED)
					m_preview.SortItems(DoSortIconsFunc, 0);
				else
					m_preview.SortItems(DoSortIconsFuncByOrder, 0);
			}
			break;

		case DM_UPDATEICONSPREVIEW: // Refresh preview to new section
			{
				LVITEM lvi = { 0 };
				HICON hIcon;
				int indx, count;
				HIMAGELIST hIml = m_preview.GetImageList(LVSIL_NORMAL);

				lvi.mask = LVIF_IMAGE | LVIF_PARAM;
				count = m_preview.GetItemCount();

				for (indx = 0; indx < count; indx++) {
					lvi.iItem = indx;
					m_preview.GetItem(&lvi);
					{
						mir_cslock lck(csIconList);
						hIcon = iconList[lvi.lParam]->temp_icon;
						if (!hIcon)
							hIcon = IconItem_GetIcon_Preview(iconList[lvi.lParam]);
					}

					if (hIcon)
						ImageList_ReplaceIcon(hIml, lvi.iImage, hIcon);
					if (hIcon != iconList[lvi.lParam]->temp_icon)
						SafeDestroyIcon(hIcon);
				}
				m_preview.RedrawItems(0, count);
			}
			break;

			// Temporary change icon - only inside options dialog
		case DM_CHANGEICON:
			{
				LVITEM lvi = { 0 };
				lvi.mask = LVIF_PARAM;
				lvi.iItem = wParam;
				m_preview.GetItem(&lvi);
				{
					mir_cslock lck(csIconList);

					IcolibItem *item = iconList[lvi.lParam];
					SafeDestroyIcon(item->temp_icon);

					wchar_t *path = (wchar_t*)lParam;
					replaceStrW(item->temp_file, path);
					item->temp_icon = (HICON)ExtractIconFromPath(path, item->cx, item->cy);
					item->temp_reset = false;
				}
				DoOptionsChanged(m_hwnd);
			}
			break;

		case WM_NOTIFY:
			if (bNeedRebuild) {
				bNeedRebuild = false;
				RebuildTree();
			}
			break;

		case WM_CONTEXTMENU:
			if ((HWND)wParam == m_preview.GetHwnd()) {
				UINT count = m_preview.GetSelectedCount();

				if (count > 0) {
					int cmd = OpenPopupMenu(m_hwnd);
					switch (cmd) {
					case ID_CANCELCHANGE:
					case ID_RESET:
						{
							LVITEM lvi = { 0 };
							int itemIndx = -1;

							while ((itemIndx = m_preview.GetNextItem(itemIndx, LVNI_SELECTED)) != -1) {
								lvi.mask = LVIF_PARAM;
								lvi.iItem = itemIndx; //lvhti.iItem;
								m_preview.GetItem(&lvi);

								UndoChanges(lvi.lParam, cmd);
							}

							DoOptionsChanged(m_hwnd);
							break;
						}
					}
				}
			}
			else {
				if ((HWND)wParam == m_categoryList.GetHwnd()) {
					int cmd = OpenPopupMenu(m_hwnd);

					switch (cmd) {
					case ID_CANCELCHANGE:
					case ID_RESET:
						UndoSubItemChanges(m_categoryList.GetHwnd(), m_categoryList.GetSelection(), cmd);
						DoOptionsChanged(m_hwnd);
						break;
					}
				}
			}
			break;
		}

		return CDlgBase::DlgProc(msg, wParam, lParam);
	}

	void OnImport(void*)
	{
		m_pDialog = new CIconImportDlg(m_hwnd);
		m_pDialog->Show();
		m_btnImport.Disable();
	}

	void OnGetMore(void*)
	{
		Utils_OpenUrl("http://miranda-ng.org/");
	}

	void OnLoadIcons(void*)
	{
		wchar_t filetmp[1] = { 0 };
		if (wchar_t *file = OpenFileDlg(m_hwnd, filetmp, FALSE)) {
			HWND htv = GetDlgItem(m_hwnd, IDC_CATEGORYLIST);
			wchar_t filename[MAX_PATH];

			PathToRelativeT(file, filename);
			mir_free(file);

			MySetCursor(IDC_WAIT);
			LoadSubIcons(htv, filename, TreeView_GetSelection(htv));
			MySetCursor(IDC_ARROW);

			DoOptionsChanged(m_hwnd);
		}
	}

	virtual void OnApply() override
	{
		{
			mir_cslock lck(csIconList);

			for (int indx = 0; indx < iconList.getCount(); indx++) {
				IcolibItem *item = iconList[indx];
				if (item->temp_reset) {
					db_unset(NULL, "SkinIcons", item->name);
					if (item->source_small != item->default_icon) {
						item->source_small->release();
						item->source_small = NULL;
					}
				}
				else if (item->temp_file) {
					db_set_ws(NULL, "SkinIcons", item->name, item->temp_file);
					item->source_small->release();
					item->source_small = NULL;
					SafeDestroyIcon(item->temp_icon);
				}
			}
		}

		DoIconsChanged(m_hwnd);
	}

	virtual void OnDestroy() override
	{
		HTREEITEM hti = m_categoryList.GetRoot();
		while (hti != NULL) {
			TVITEMEX tvi;
			tvi.mask = TVIF_STATE | TVIF_HANDLE | TVIF_CHILDREN | TVIF_PARAM;
			tvi.hItem = hti;
			tvi.stateMask = (DWORD)-1;
			m_categoryList.GetItem(&tvi);

			if (tvi.cChildren > 0) {
				TreeItem *treeItem = (TreeItem *)tvi.lParam;
				if (tvi.state & TVIS_EXPANDED)
					db_set_b(NULL, "SkinIconsUI", treeItem->paramName, TVIS_EXPANDED);
				else
					db_set_b(NULL, "SkinIconsUI", treeItem->paramName, 0);
			}

			HTREEITEM ht = m_categoryList.GetChild(hti);
			if (ht == NULL) {
				ht = m_categoryList.GetNextSibling(hti);
				while (ht == NULL) {
					hti = m_categoryList.GetParent(hti);
					if (hti == NULL)
						break;
					
					ht = m_categoryList.GetNextSibling(hti);
				}
			}

			hti = ht;
		}

		if (m_pDialog)
			m_pDialog->Close();

		mir_cslock lck(csIconList);
		for (int indx = 0; indx < iconList.getCount(); indx++) {
			IcolibItem *item = iconList[indx];

			replaceStrW(item->temp_file, NULL);
			SafeDestroyIcon(item->temp_icon);
		}
	}

	void OnGetInfoTip(CCtrlListView::TEventInfo *evt)
	{
		NMLVGETINFOTIP *pInfoTip = evt->nmlvit;
		LVITEM lvi;
		lvi.mask = LVIF_PARAM;
		lvi.iItem = pInfoTip->iItem;
		ListView_GetItem(pInfoTip->hdr.hwndFrom, &lvi);

		if (lvi.lParam < iconList.getCount()) {
			IcolibItem *item = iconList[lvi.lParam];
			if (item->temp_file)
				wcsncpy_s(pInfoTip->pszText, pInfoTip->cchTextMax, item->temp_file, _TRUNCATE);
			else if (item->default_file)
				mir_snwprintf(pInfoTip->pszText, pInfoTip->cchTextMax, L"%s, %d", item->default_file->file, item->default_indx);
		}
	}

	void OnCategoryChanged(CCtrlTreeView::TEventInfo *evt)
	{
		NMTREEVIEW *pnmtv = evt->nmtv;
		TVITEM tvi = pnmtv->itemNew;
		TreeItem *treeItem = (TreeItem *)tvi.lParam;
		if (treeItem)
			SendMessage(m_hwnd, DM_REBUILDICONSPREVIEW, 0, (LPARAM)((SECTIONPARAM_FLAGS(treeItem->value) & SECTIONPARAM_HAVEPAGE) ? sectionList[SECTIONPARAM_INDEX(treeItem->value)] : NULL));
	}

	void OnCategoryDeleted(CCtrlTreeView::TEventInfo *evt)
	{
		TreeItem *treeItem = (TreeItem *)(evt->nmtv->itemOld.lParam);
		if (treeItem) {
			mir_free(treeItem->paramName);
			mir_free(treeItem);
		}
	}
};

int SkinOptionsInit(WPARAM wParam, LPARAM)
{
	OPTIONSDIALOGPAGE odp = { 0 };
	odp.flags = ODPF_BOLDGROUPS;
	odp.position = -180000000;
	odp.pDialog = new CIcoLibOptsDlg();
	odp.szTitle.a = LPGEN("Icons");
	Options_AddPage(wParam, &odp);
	return 0;
}
