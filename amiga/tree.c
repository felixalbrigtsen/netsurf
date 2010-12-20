/*
 * Copyright 2008 - 2010 Chris Young <chris@unsatisfactorysoftware.co.uk>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <proto/window.h>
#include <proto/layout.h>
#include <proto/space.h>
#include <proto/label.h>
#include <proto/scroller.h>
#include <classes/window.h>
#include <gadgets/space.h>
#include <images/label.h>
#include <gadgets/layout.h>
#include <gadgets/scroller.h>
#include <reaction/reaction_macros.h>
#include "amiga/gui.h"
#include "content/urldb.h"
#include <proto/exec.h>
#include <assert.h>
#include <proto/intuition.h>
#include "amiga/tree.h"
#include <proto/button.h>
#include <gadgets/button.h>
#include <string.h>
#include "utils/messages.h"
#include <proto/bitmap.h>
#include <images/bitmap.h>
#include <proto/graphics.h>
#include <intuition/icclass.h>
#include <proto/asl.h>
#include <proto/utility.h>
#include <libraries/gadtools.h>
#include <proto/dos.h>
#include "amiga/utf8.h"
#include "desktop/cookies.h"
#include "desktop/history_global_core.h"
#include "desktop/hotlist.h"
#include "desktop/tree_url_node.h"
#include "amiga/sslcert.h"
#include "amiga/drag.h" /* drag icon stuff */
#include "amiga/theme.h" /* pointers */
#include "amiga/filetype.h"
#include "amiga/options.h"
#include "utils/utils.h"

#define AMI_TREE_MENU_ITEMS 21
#define AMI_TREE_MENU_DELETE FULLMENUNUM(1,0,0)
#define AMI_TREE_MENU_CLEAR FULLMENUNUM(1,3,0)

struct treeview_window {
	struct Window *win;
	Object *objects[OID_LAST];
	struct Gadget *gadgets[GID_LAST];
	struct nsObject *node;
	int type;
	struct NewMenu *menu;
	char *menu_name[AMI_TREE_MENU_ITEMS];
	struct tree *tree;
	struct Hook scrollerhook;
	uint32 key_state;
	uint32 mouse_state;
	int drag_x;
	int drag_y;
	struct timeval lastclick;
	int max_width;
	int max_height;
	struct gui_globals globals;
	struct sslcert_session_data *ssl_data;
};

void ami_tree_draw(struct treeview_window *twin);
static void ami_tree_redraw_request(int x, int y, int width, int height,
		void *data);
static void ami_tree_resized(struct tree *tree, int width,
		int height, void *data);
static void ami_tree_scroll_visible(int y, int height, void *data);
static void ami_tree_get_window_dimensions(int *width, int *height, void *data);

const struct treeview_table ami_tree_callbacks = {
	.redraw_request = ami_tree_redraw_request,
	.resized = ami_tree_resized,
	.scroll_visible = ami_tree_scroll_visible,
	.get_window_dimensions = ami_tree_get_window_dimensions
};

struct treeview_window *ami_tree_create(uint8 flags,
			struct sslcert_session_data *ssl_data)
{
	struct treeview_window *twin;

	twin = AllocVec(sizeof(struct treeview_window),
					MEMF_PRIVATE | MEMF_CLEAR);

	if(!twin)
	{
		warn_user("NoMemory", 0);
		return NULL;
	}

	twin->ssl_data = ssl_data;

	twin->tree = tree_create(flags, &ami_tree_callbacks, twin);
	return twin;
}

void ami_tree_destroy(struct treeview_window *twin)
{
	tree_delete(twin->tree);
	FreeVec(twin);
}

struct tree *ami_tree_get_tree(struct treeview_window *twin)
{
	return twin->tree;
}

void ami_tree_resized(struct tree *tree, int width, int height, void *data)
{
	struct treeview_window *twin = data;
	struct IBox *bbox;

	twin->max_height = height;
	twin->max_width = width;

	if(twin->win)
	{
		GetAttr(SPACE_AreaBox,twin->gadgets[GID_BROWSER],(ULONG *)&bbox);

		RefreshSetGadgetAttrs((APTR)twin->objects[OID_VSCROLL], twin->win, NULL,
			SCROLLER_Total, height,
			SCROLLER_Visible, bbox->Height,
			TAG_DONE);

		RefreshSetGadgetAttrs((APTR)twin->objects[OID_HSCROLL], twin->win, NULL,
			SCROLLER_Total, width,
			SCROLLER_Visible, bbox->Width,
			TAG_DONE);
	}
}

/**
 * Retrieves the dimensions of the window with the tree
 *
 * \param data	user data assigned to the tree on tree creation
 * \param width	will be updated to window width if not NULL
 * \param height	will be updated to window height if not NULL
 */
void ami_tree_get_window_dimensions(int *width, int *height, void *data)
{
	struct treeview_window *twin = data;
	struct IBox *bbox;

	GetAttr(SPACE_AreaBox,twin->gadgets[GID_BROWSER],(ULONG *)&bbox);

	if(width) *width = bbox->Width;
	if(height) *height = bbox->Height;
}

/**
 * Translates a content_type to the name of a respective icon
 *
 * \param content_type	content type
 * \param buffer	buffer for the icon name
 */
void tree_icon_name_from_content_type(char *buffer, content_type type)
{
	const char *ftype = ami_content_type_to_file_type(type);
	sprintf(buffer, "def_%s.info", ftype);
}

/**
 * Scrolls the tree to make an element visible
 *
 * \param y	Y coordinate of the element
 * \param height	height of the element
 * \param data	user data assigned to the tree on tree creation
 */
void ami_tree_scroll_visible(int y, int height, void *data)
{
	ULONG sy, scrollset;
	struct IBox *bbox;
	struct treeview_window *twin = data;

	GetAttr(SCROLLER_Top, twin->objects[OID_VSCROLL], (ULONG *)&sy);
	GetAttr(SPACE_AreaBox,twin->gadgets[GID_BROWSER],(ULONG *)&bbox);

	if((y > sy) && ((y + height) < (sy + bbox->Height))) return;

	if((y <= sy) || (height > bbox->Height)) scrollset = (ULONG)y;
		else scrollset = sy + (y + height) - (sy + bbox->Height);

	RefreshSetGadgetAttrs((APTR)twin->objects[OID_VSCROLL], twin->win, NULL,
			SCROLLER_Top, scrollset,
			TAG_DONE);

	ami_tree_draw(twin);
}

void ami_tree_scroll(struct treeview_window *twin, int sx, int sy)
{
	int x, y;

	if(!twin) return;

	GetAttr(SCROLLER_Top, twin->objects[OID_HSCROLL], (ULONG *)&x);
	GetAttr(SCROLLER_Top, twin->objects[OID_VSCROLL], (ULONG *)&y);

	x += sx;
	y += sy;

	if(y < 0) y = 0;
	if(x < 0) x = 0;

	RefreshSetGadgetAttrs((APTR)twin->objects[OID_VSCROLL], twin->win, NULL,
			SCROLLER_Top, y,
			TAG_DONE);

	RefreshSetGadgetAttrs((APTR)twin->objects[OID_HSCROLL], twin->win, NULL,
			SCROLLER_Top, x,
			TAG_DONE);

	ami_tree_draw(twin);
}

void ami_tree_drag_icon_show(struct treeview_window *twin)
{
	const char *type = "project";
	const char *url;
	struct url_data *data;
	struct node *node = NULL;

	if((tree_drag_status(twin->tree) == TREE_NO_DRAG) ||
		(tree_drag_status(twin->tree) == TREE_SELECT_DRAG) ||
		(tree_drag_status(twin->tree) == TREE_TEXTAREA_DRAG))
		return;

	if((twin->type == AMI_TREE_COOKIES) ||
		(twin->type == AMI_TREE_SSLCERT)) return; /* No permissable drag operations */

	node = tree_get_selected_node(tree_get_root(twin->tree));

	if(node && tree_node_is_folder(node))
	{
		ami_drag_icon_show(twin->win, "drawer");
	}
	else
	{
		if(node && (url = tree_url_node_get_url(node)))
		{
			if(data = urldb_get_url_data(url))
			{
				type = ami_content_type_to_file_type(data->type);
			}
		}
		ami_drag_icon_show(twin->win, type);
	}
}

void ami_tree_drag_end(struct treeview_window *twin, int x, int y)
{
	struct gui_window_2 *gwin;
	struct node *selected_node;
	BOOL drag;

	if(drag = ami_drag_in_progress()) ami_drag_icon_close(twin->win);

	if(drag && (gwin = ami_window_at_pointer()))
	{
		selected_node = tree_get_selected_node(tree_get_root(twin->tree));

		if((selected_node == NULL) || (tree_node_is_folder(selected_node)))
		{
			DisplayBeep(scrn);
		}
		else
		{
			browser_window_go(gwin->bw, tree_url_node_get_url(selected_node),
				NULL, true);
		}
		tree_drag_end(twin->tree, twin->mouse_state,
			twin->drag_x, twin->drag_y,
			twin->drag_x, twin->drag_y); /* Keep the tree happy */
	}
	else
	{
		if(tree_drag_status(twin->tree) == TREE_UNKNOWN_DRAG)
			DisplayBeep(scrn);

		tree_drag_end(twin->tree, twin->mouse_state,
			twin->drag_x, twin->drag_y, x, y);
	}
}

void ami_tree_scroller_hook(struct Hook *hook,Object *object,struct IntuiMessage *msg) 
{
	ULONG gid,x,y;
	struct treeview_window *twin = hook->h_Data;
	struct IntuiWheelData *wheel;

	switch(msg->Class)
	{
		case IDCMP_IDCMPUPDATE:
			gid = GetTagData( GA_ID, 0, msg->IAddress ); 

			switch( gid ) 
			{ 
 				case OID_HSCROLL: 
 				case OID_VSCROLL:
					ami_tree_draw(twin);
 				break; 
			} 
		break;

		case IDCMP_EXTENDEDMOUSE:
			if(msg->Code == IMSGCODE_INTUIWHEELDATA)
			{
				wheel = (struct IntuiWheelData *)msg->IAddress;

				ami_tree_scroll(twin, (wheel->WheelX * 20), (wheel->WheelY * 20));
			}
		break;
	}
} 

void ami_tree_menu(struct treeview_window *twin)
{
	if(twin->menu) return;

	if(twin->menu = AllocVec(sizeof(struct NewMenu) * AMI_TREE_MENU_ITEMS, MEMF_CLEAR))
	{
		twin->menu[0].nm_Type = NM_TITLE;
		twin->menu_name[0] = ami_utf8_easy((char *)messages_get("Tree"));
		twin->menu[0].nm_Label = twin->menu_name[0];

		twin->menu[1].nm_Type = NM_ITEM;
		twin->menu_name[1] = ami_utf8_easy((char *)messages_get("TreeExport"));
		twin->menu[1].nm_Label = twin->menu_name[1];
		if(twin->type == AMI_TREE_COOKIES)
			twin->menu[1].nm_Flags = NM_ITEMDISABLED;
		twin->menu[1].nm_CommKey = "S";

		twin->menu[2].nm_Type = NM_ITEM;
		twin->menu[2].nm_Label = NM_BARLABEL;

		twin->menu[3].nm_Type = NM_ITEM;
		twin->menu_name[3] = ami_utf8_easy((char *)messages_get("Expand"));
		twin->menu[3].nm_Label = twin->menu_name[3];

		twin->menu[4].nm_Type = NM_SUB;
		twin->menu_name[4] = ami_utf8_easy((char *)messages_get("All"));
		twin->menu[4].nm_Label = twin->menu_name[4];
		twin->menu[4].nm_CommKey = "+";

		if(twin->type == AMI_TREE_COOKIES)
		{
			twin->menu_name[5] = ami_utf8_easy((char *)messages_get("Domains"));
			twin->menu_name[6] = ami_utf8_easy((char *)messages_get("Cookies"));
		}
		else
		{
			twin->menu_name[5] = ami_utf8_easy((char *)messages_get("Folders"));
			twin->menu_name[6] = ami_utf8_easy((char *)messages_get("Links"));
		}

		twin->menu[5].nm_Type = NM_SUB;
		twin->menu[5].nm_Label = twin->menu_name[5]; // tree-specific title

		twin->menu[6].nm_Type = NM_SUB;
		twin->menu[6].nm_Label = twin->menu_name[6]; // tree-specific title

		twin->menu[7].nm_Type = NM_ITEM;
		twin->menu_name[7] = ami_utf8_easy((char *)messages_get("Collapse"));
		twin->menu[7].nm_Label = twin->menu_name[7];

		twin->menu[8].nm_Type = NM_SUB;
		twin->menu[8].nm_Label = twin->menu_name[4];
		twin->menu[8].nm_CommKey = "-";

		twin->menu[9].nm_Type = NM_SUB;
		twin->menu[9].nm_Label = twin->menu_name[5]; // tree-specific title

		twin->menu[10].nm_Type = NM_SUB;
		twin->menu[10].nm_Label = twin->menu_name[6]; // tree-specific title

		twin->menu[11].nm_Type = NM_ITEM;
		twin->menu[11].nm_Label = NM_BARLABEL;

		twin->menu[12].nm_Type = NM_ITEM;
		twin->menu_name[12] = ami_utf8_easy((char *)messages_get("SnapshotWindow"));
		twin->menu[12].nm_Label = twin->menu_name[12];

		twin->menu[13].nm_Type = NM_ITEM;
		twin->menu[13].nm_Label = NM_BARLABEL;

		twin->menu[14].nm_Type = NM_ITEM;
		twin->menu_name[14] = ami_utf8_easy((char *)messages_get("CloseWindow"));
		twin->menu[14].nm_Label = twin->menu_name[14];
		twin->menu[14].nm_CommKey = "K";

		twin->menu[15].nm_Type = NM_TITLE;
		twin->menu_name[15] = ami_utf8_easy((char *)messages_get("Edit"));
		twin->menu[15].nm_Label = twin->menu_name[15];

		twin->menu[16].nm_Type = NM_ITEM;
		twin->menu_name[16] = ami_utf8_easy((char *)messages_get("TreeDelete"));
		twin->menu[16].nm_Label = twin->menu_name[16];
		twin->menu[16].nm_CommKey = "D";

		twin->menu[17].nm_Type = NM_ITEM;
		twin->menu[17].nm_Label = NM_BARLABEL;

		twin->menu[18].nm_Type = NM_ITEM;
		twin->menu_name[18] = ami_utf8_easy((char *)messages_get("SelectAllNS"));
		twin->menu[18].nm_Label = twin->menu_name[18];
		twin->menu[18].nm_CommKey = "A";

		twin->menu[19].nm_Type = NM_ITEM;
		twin->menu_name[19] = ami_utf8_easy((char *)messages_get("ClearNS"));
		twin->menu[19].nm_Label = twin->menu_name[19];
		twin->menu[19].nm_CommKey = "Z";

		twin->menu[20].nm_Type = NM_END;
	}
}

void ami_tree_update_buttons(struct treeview_window *twin)
{
	BOOL launch_disable = FALSE;

	if(twin->type == AMI_TREE_SSLCERT) return;

	if(tree_node_has_selection(tree_get_root(twin->tree)))
	{
		struct node *selected_node =
			tree_get_selected_node(tree_get_root(twin->tree));

		OnMenu(twin->win, AMI_TREE_MENU_DELETE);
		OnMenu(twin->win, AMI_TREE_MENU_CLEAR);

		RefreshSetGadgetAttrs((struct Gadget *)twin->gadgets[GID_DEL],
			twin->win, NULL,
			GA_Disabled, FALSE,
			TAG_DONE);

		if((selected_node && (tree_node_is_folder(selected_node) == true)))
			launch_disable = TRUE;
	}
	else
	{
		OffMenu(twin->win, AMI_TREE_MENU_DELETE);
		OffMenu(twin->win, AMI_TREE_MENU_CLEAR);

		RefreshSetGadgetAttrs((struct Gadget *)twin->gadgets[GID_DEL],
			twin->win, NULL,
			GA_Disabled, TRUE,
			TAG_DONE);

		launch_disable = TRUE;
	}

	if(twin->type != AMI_TREE_COOKIES)
	{
		RefreshSetGadgetAttrs((struct Gadget *)twin->gadgets[GID_OPEN],
			twin->win, NULL,
			GA_Disabled, launch_disable,
			TAG_DONE);
	}
}

void ami_tree_open(struct treeview_window *twin,int type)
{
	BOOL msel = TRUE,nothl = TRUE,launchdisable=FALSE;
	static WORD gen=0;
	char *wintitle;
	char folderclosed[100],folderopen[100],item[100];

	if(twin->win)
	{
		WindowToFront(twin->win);
		ActivateWindow(twin->win);
		return;
	}

	twin->type = type;

	switch(twin->type)
	{
		case AMI_TREE_HOTLIST:
			nothl = FALSE;
			wintitle = (char *)messages_get("Hotlist");
		break;
		case AMI_TREE_COOKIES:
			nothl = TRUE;
			launchdisable=TRUE;
			wintitle = (char *)messages_get("Cookies");
		break;
		case AMI_TREE_HISTORY:
			nothl = TRUE;
			wintitle = (char *)messages_get("GlobalHistory");
		break;
		case AMI_TREE_SSLCERT:
			nothl = TRUE;
			wintitle = (char *)messages_get("SSLCerts");
		break;
	}

	twin->scrollerhook.h_Entry = (void *)ami_tree_scroller_hook;
	twin->scrollerhook.h_Data = twin;

	ami_init_layers(&twin->globals, scrn->Width, scrn->Height);
	ami_tree_menu(twin);

	if(type == AMI_TREE_SSLCERT)
	{
		twin->objects[OID_MAIN] = WindowObject,
      	    WA_ScreenTitle,nsscreentitle,
           	WA_Title,wintitle,
           	WA_Activate, TRUE,
           	WA_DepthGadget, TRUE,
           	WA_DragBar, TRUE,
           	WA_CloseGadget, TRUE,
           	WA_SizeGadget, TRUE,
			WA_Height, scrn->Height / 2,
			WA_CustomScreen,scrn,
			WA_ReportMouse,TRUE,
           	WA_IDCMP, IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS | IDCMP_NEWSIZE |
					IDCMP_RAWKEY | IDCMP_GADGETUP | IDCMP_IDCMPUPDATE |
					IDCMP_EXTENDEDMOUSE | IDCMP_SIZEVERIFY,
			WINDOW_HorizProp,1,
			WINDOW_VertProp,1,
			WINDOW_IDCMPHook,&twin->scrollerhook,
			WINDOW_IDCMPHookBits,IDCMP_IDCMPUPDATE | IDCMP_EXTENDEDMOUSE,
			WINDOW_SharedPort,sport,
			WINDOW_UserData,twin,
			/* WINDOW_NewMenu, twin->menu,   -> No menu for SSL Cert */
			WINDOW_IconifyGadget, FALSE,
			WINDOW_Position, WPOS_CENTERSCREEN,
			WINDOW_ParentGroup, twin->gadgets[GID_MAIN] = VGroupObject,
				LAYOUT_AddImage, LabelObject,
					LABEL_Text, messages_get("SSLError"),
				LabelEnd,
				LAYOUT_AddChild, twin->gadgets[GID_BROWSER] = SpaceObject,
					GA_ID, GID_BROWSER,
					SPACE_Transparent,TRUE,
					SPACE_BevelStyle, BVS_DISPLAY,
       			SpaceEnd,
				LAYOUT_AddChild, HGroupObject,
					LAYOUT_AddChild, twin->gadgets[GID_OPEN] = ButtonObject,
						GA_ID,GID_OPEN,
						GA_Text,messages_get("Accept"),
						GA_RelVerify,TRUE,
					ButtonEnd,
					LAYOUT_AddChild, twin->gadgets[GID_CANCEL] = ButtonObject,
						GA_ID,GID_CANCEL,
						GA_Text,messages_get("Reject"),
						GA_RelVerify,TRUE,
					ButtonEnd,
				EndGroup,
				CHILD_WeightedHeight,0,
			EndGroup,
		EndWindow;
	}
	else
	{
		ULONG width = scrn->Width / 2;
		ULONG height = scrn->Height / 2;
		ULONG top = (scrn->Height / 2) - (height / 2);
		ULONG left = (scrn->Width / 2) - (width / 2);

		if((type == AMI_TREE_HOTLIST) && (option_hotlist_window_xsize > 0))
		{
			top = option_hotlist_window_ypos;
			left = option_hotlist_window_xpos;
			width = option_hotlist_window_xsize;
			height = option_hotlist_window_ysize;
		}
		else if((type == AMI_TREE_HISTORY) && (option_history_window_xsize > 0))
		{
			top = option_history_window_ypos;
			left = option_history_window_xpos;
			width = option_history_window_xsize;
			height = option_history_window_ysize;
		}
		else if((type == AMI_TREE_COOKIES) && (option_cookies_window_xsize > 0))
		{
			top = option_cookies_window_ypos;
			left = option_cookies_window_xpos;
			width = option_cookies_window_xsize;
			height = option_cookies_window_ysize;
		}

		twin->objects[OID_MAIN] = WindowObject,
      	    WA_ScreenTitle,nsscreentitle,
           	WA_Title,wintitle,
           	WA_Activate, TRUE,
           	WA_DepthGadget, TRUE,
           	WA_DragBar, TRUE,
           	WA_CloseGadget, TRUE,
           	WA_SizeGadget, TRUE,
			WA_Top, top,
			WA_Left, left,
			WA_Width, width,
			WA_Height, height,
			WA_CustomScreen,scrn,
			WA_ReportMouse,TRUE,
           	WA_IDCMP, IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS | IDCMP_NEWSIZE |
					IDCMP_RAWKEY | IDCMP_GADGETUP | IDCMP_IDCMPUPDATE |
					IDCMP_EXTENDEDMOUSE | IDCMP_SIZEVERIFY | IDCMP_INTUITICKS,
			WINDOW_HorizProp,1,
			WINDOW_VertProp,1,
			WINDOW_IDCMPHook,&twin->scrollerhook,
			WINDOW_IDCMPHookBits,IDCMP_IDCMPUPDATE | IDCMP_EXTENDEDMOUSE,
			WINDOW_SharedPort,sport,
			WINDOW_UserData,twin,
			WINDOW_NewMenu, twin->menu,
			WINDOW_IconifyGadget, FALSE,
//			WINDOW_Position, WPOS_CENTERSCREEN,
			WINDOW_ParentGroup, twin->gadgets[GID_MAIN] = VGroupObject,
				LAYOUT_AddChild, twin->gadgets[GID_BROWSER] = SpaceObject,
					GA_ID, GID_BROWSER,
					SPACE_Transparent,TRUE,
					SPACE_BevelStyle, BVS_DISPLAY,
       			SpaceEnd,
				LAYOUT_AddChild, HGroupObject,
					LAYOUT_AddChild, twin->gadgets[GID_OPEN] = ButtonObject,
						GA_ID,GID_OPEN,
						GA_Text,messages_get("TreeLaunch"),
						GA_RelVerify,TRUE,
						GA_Disabled,launchdisable,
					ButtonEnd,
					LAYOUT_AddChild, twin->gadgets[GID_NEWF] = ButtonObject,
						GA_ID,GID_NEWF,
						BUTTON_AutoButton,BAG_POPDRAWER,
						GA_RelVerify,TRUE,
						GA_Disabled,nothl,
					ButtonEnd,
					LAYOUT_AddChild, twin->gadgets[GID_NEWB] = ButtonObject,
						GA_ID,GID_NEWB,
						BUTTON_AutoButton,BAG_POPFILE,
						GA_RelVerify,TRUE,
						GA_Disabled,nothl,
					ButtonEnd,
					LAYOUT_AddChild, twin->gadgets[GID_DEL] = ButtonObject,
						GA_ID,GID_DEL,
						GA_Text,messages_get("TreeDelete"),
						GA_RelVerify,TRUE,
					ButtonEnd,
				EndGroup,
				CHILD_WeightedHeight,0,
			EndGroup,
		EndWindow;
	}

	twin->win = (struct Window *)RA_OpenWindow(twin->objects[OID_MAIN]);

	GetAttr(WINDOW_HorizObject, twin->objects[OID_MAIN],
				(ULONG *)&twin->objects[OID_HSCROLL]);
	GetAttr(WINDOW_VertObject, twin->objects[OID_MAIN],
				(ULONG *)&twin->objects[OID_VSCROLL]);

	RefreshSetGadgetAttrs((APTR)twin->objects[OID_VSCROLL],	twin->win,	NULL,
		GA_ID,OID_VSCROLL,
		ICA_TARGET,ICTARGET_IDCMP,
		TAG_DONE);

	RefreshSetGadgetAttrs((APTR)twin->objects[OID_HSCROLL], twin->win, NULL,
		GA_ID,OID_HSCROLL,
		ICA_TARGET,ICTARGET_IDCMP,
		TAG_DONE);

	twin->node = AddObject(window_list,AMINS_TVWINDOW);
	twin->node->objstruct = twin;

	ami_tree_update_buttons(twin);
	ami_tree_resized(twin->tree, twin->max_width, twin->max_height, twin);
	tree_set_redraw(twin->tree, true);
	ami_tree_draw(twin);
}

void ami_tree_close(struct treeview_window *twin)
{
	int i;

	tree_set_redraw(twin->tree, false);
	twin->win = NULL;
	DisposeObject(twin->objects[OID_MAIN]);
	DelObjectNoFree(twin->node);
	ami_free_layers(&twin->globals);

	for(i=0;i<AMI_TREE_MENU_ITEMS;i++)
	{
		if(twin->menu_name[i] && (twin->menu_name[i] != NM_BARLABEL)) ami_utf8_free(twin->menu_name[i]);
	}
	FreeVec(twin->menu);
	twin->menu = NULL;
	if(twin->type == AMI_TREE_SSLCERT) ami_ssl_free(twin);
}

void ami_tree_update_quals(struct treeview_window *twin)
{
	uint32 quals = 0;

	GetAttr(WINDOW_Qualifier, twin->objects[OID_MAIN], (uint32 *)&quals);

	twin->key_state = 0;

	if((quals & IEQUALIFIER_LSHIFT) || (quals & IEQUALIFIER_RSHIFT)) 
	{
		twin->key_state |= BROWSER_MOUSE_MOD_1;
	}

	if(quals & IEQUALIFIER_CONTROL) 
	{
		twin->key_state |= BROWSER_MOUSE_MOD_2;
	}

	if((quals & IEQUALIFIER_LALT) || (quals & IEQUALIFIER_RALT)) 
	{
		twin->key_state |= BROWSER_MOUSE_MOD_3;
	}
}

BOOL ami_tree_event(struct treeview_window *twin)
{
	/* return TRUE if window destroyed */
	ULONG class,result,storage = 0;
	uint16 code;
	struct MenuItem *item;
	ULONG menunum=0,itemnum=0,subnum=0;
	int xs, ys, x, y;
	struct IBox *bbox;
	struct timeval curtime;
	struct InputEvent *ie;
	int nskey;
	char fname[1024];
	static int drag_x_move = 0, drag_y_move = 0;

	while((result = RA_HandleInput(twin->objects[OID_MAIN],&code)) != WMHI_LASTMSG)
	{
       	switch(result & WMHI_CLASSMASK) // class
   		{
			case WMHI_GADGETUP:
				switch(result & WMHI_GADGETMASK)
				{
					case GID_OPEN:
						if(twin->type == AMI_TREE_SSLCERT)
						{
							sslcert_accept(twin->ssl_data);
							ami_tree_close(twin);
							return TRUE;
						}
						else tree_launch_selected(twin->tree);
					break;

					case GID_CANCEL:
						sslcert_reject(twin->ssl_data);
						ami_tree_close(twin);
						return TRUE;
					break;

					case GID_NEWF:
						hotlist_add_folder();
					break;

					case GID_NEWB:
						hotlist_add_entry();
					break;

					case GID_DEL:
						switch(twin->type)
						{
							case AMI_TREE_HISTORY:
								history_global_delete_selected();
							break;
							case AMI_TREE_COOKIES:
								cookies_delete_selected();
							break;
							case AMI_TREE_HOTLIST:
								hotlist_delete_selected();
							break;
						}
					break;
				}
			break;

			case WMHI_MOUSEMOVE:
				drag_x_move = 0;
				drag_y_move = 0;

				GetAttr(SPACE_AreaBox, twin->gadgets[GID_BROWSER], (ULONG *)&bbox);

				GetAttr(SCROLLER_Top, twin->objects[OID_HSCROLL], (ULONG *)&xs);
				x = twin->win->MouseX - bbox->Left + xs;

				GetAttr(SCROLLER_Top, twin->objects[OID_VSCROLL], (ULONG *)&ys);
				y = twin->win->MouseY - bbox->Top + ys;

				if(twin->mouse_state & BROWSER_MOUSE_DRAG_ON)
				{
					ami_drag_icon_move();

					if((twin->win->MouseX < bbox->Left) &&
						((twin->win->MouseX - bbox->Left) > -AMI_DRAG_THRESHOLD))
						drag_x_move = twin->win->MouseX - bbox->Left;
					if((twin->win->MouseX > (bbox->Left + bbox->Width)) &&
						((twin->win->MouseX - (bbox->Left + bbox->Width)) < AMI_DRAG_THRESHOLD))
						drag_x_move = twin->win->MouseX - (bbox->Left + bbox->Width);
					if((twin->win->MouseY < bbox->Top) &&
						((twin->win->MouseY - bbox->Top) > -AMI_DRAG_THRESHOLD))
						drag_y_move = twin->win->MouseY - bbox->Top;
					if((twin->win->MouseY > (bbox->Top + bbox->Height)) &&
						((twin->win->MouseY - (bbox->Top + bbox->Height)) < AMI_DRAG_THRESHOLD))
						drag_y_move = twin->win->MouseY - (bbox->Top + bbox->Height);
				}

				if((x >= xs) && (y >= ys) && (x < bbox->Width + xs) &&
					(y < bbox->Height + ys))
				{
					ami_tree_update_quals(twin);

					if(twin->mouse_state & BROWSER_MOUSE_PRESS_1)
					{
						tree_mouse_action(twin->tree,
							BROWSER_MOUSE_DRAG_1 | twin->key_state, x, y);
						twin->mouse_state = BROWSER_MOUSE_HOLDING_1 |
											BROWSER_MOUSE_DRAG_ON;
						if(twin->drag_x == 0) twin->drag_x = x;
						if(twin->drag_y == 0) twin->drag_y = y;
						ami_tree_drag_icon_show(twin);
					}
					else if(twin->mouse_state & BROWSER_MOUSE_PRESS_2)
					{
						tree_mouse_action(twin->tree,
							BROWSER_MOUSE_DRAG_2 | twin->key_state, x, y);
						twin->mouse_state = BROWSER_MOUSE_HOLDING_2 |
											BROWSER_MOUSE_DRAG_ON;
						if(twin->drag_x == 0) twin->drag_x = x;
						if(twin->drag_y == 0) twin->drag_y = y;
						ami_tree_drag_icon_show(twin);
					}
				}
				twin->lastclick.tv_sec = 0;
				twin->lastclick.tv_usec = 0;
			break;

			case WMHI_MOUSEBUTTONS:
				GetAttr(SPACE_AreaBox, twin->gadgets[GID_BROWSER], (ULONG *)&bbox);	
				GetAttr(SCROLLER_Top, twin->objects[OID_HSCROLL], (ULONG *)&xs);
				x = twin->win->MouseX - bbox->Left + xs;
				GetAttr(SCROLLER_Top, twin->objects[OID_VSCROLL], (ULONG *)&ys);
				y = twin->win->MouseY - bbox->Top + ys;

				ami_tree_update_quals(twin);

				if((x >= xs) && (y >= ys) && (x < bbox->Width + xs) &&
					(y < bbox->Height + ys))
				{
					switch(code)
					{
						case SELECTDOWN:
							twin->mouse_state = BROWSER_MOUSE_PRESS_1;
						break;
						case MIDDLEDOWN:
							twin->mouse_state = BROWSER_MOUSE_PRESS_2;
						break;
					}
				}

				if(x < xs) x = xs;
				if(y < ys) y = ys;
				if(x >= bbox->Width + xs) x = bbox->Width + xs - 1;
				if(y >= bbox->Height + ys) y = bbox->Height + ys - 1;

				switch(code)
				{
					case SELECTUP:
						if(twin->mouse_state & BROWSER_MOUSE_PRESS_1)
						{
							CurrentTime(&curtime.tv_sec,&curtime.tv_usec);

							twin->mouse_state = BROWSER_MOUSE_CLICK_1;

							if(twin->lastclick.tv_sec)
							{
								if(DoubleClick(twin->lastclick.tv_sec,
											twin->lastclick.tv_usec,
											curtime.tv_sec, curtime.tv_usec))
									twin->mouse_state |= BROWSER_MOUSE_DOUBLE_CLICK;
							}
							tree_mouse_action(twin->tree,
								twin->mouse_state | twin->key_state, x, y);

							if(twin->mouse_state & BROWSER_MOUSE_DOUBLE_CLICK)
							{
								twin->lastclick.tv_sec = 0;
								twin->lastclick.tv_usec = 0;
							}
							else
							{
								twin->lastclick.tv_sec = curtime.tv_sec;
								twin->lastclick.tv_usec = curtime.tv_usec;
							}
						}
						else ami_tree_drag_end(twin, x, y);

						twin->mouse_state=0;
						twin->drag_x = 0;
						twin->drag_y = 0;
					break;

					case MIDDLEUP:
						if(twin->mouse_state & BROWSER_MOUSE_PRESS_2)
						{
							CurrentTime(&curtime.tv_sec,&curtime.tv_usec);

							twin->mouse_state = BROWSER_MOUSE_CLICK_2;

							if(twin->lastclick.tv_sec)
							{
								if(DoubleClick(twin->lastclick.tv_sec,
											twin->lastclick.tv_usec,
											curtime.tv_sec, curtime.tv_usec))
									twin->mouse_state |= BROWSER_MOUSE_DOUBLE_CLICK;
							}
							tree_mouse_action(twin->tree,
								twin->mouse_state | twin->key_state, x, y);

							if(twin->mouse_state & BROWSER_MOUSE_DOUBLE_CLICK)
							{
								twin->lastclick.tv_sec = 0;
								twin->lastclick.tv_usec = 0;
							}
							else
							{
								twin->lastclick.tv_sec = curtime.tv_sec;
								twin->lastclick.tv_usec = curtime.tv_usec;
							}
						}
						else ami_tree_drag_end(twin, x, y);

						twin->mouse_state=0;
						twin->drag_x = 0;
						twin->drag_y = 0;
					break;

					case SELECTDOWN:
					case MIDDLEDOWN:
						tree_mouse_action(twin->tree,
							twin->mouse_state | twin->key_state, x, y);
					break;
				}
				ami_tree_update_buttons(twin);
			break;

			case WMHI_RAWKEY:
				storage = result & WMHI_GADGETMASK;

				GetAttr(WINDOW_InputEvent,twin->objects[OID_MAIN],(ULONG *)&ie);
				nskey = ami_key_to_nskey(storage, ie);
				tree_keypress(twin->tree, nskey);
			break;

			case WMHI_MENUPICK:
				item = ItemAddress(twin->win->MenuStrip,code);
				while (code != MENUNULL)
				{
					menunum = MENUNUM(code);
					itemnum = ITEMNUM(code);
					subnum = SUBNUM(code);

					switch(menunum)
					{
						case 0: // tree
							switch(itemnum)
							{
								case 0: // export
									if(AslRequestTags(savereq,
										ASLFR_TitleText,messages_get("NetSurf"),
										ASLFR_Screen,scrn,
										ASLFR_InitialFile,"tree_export.html",
										TAG_DONE))
									{
										strlcpy(&fname,savereq->fr_Drawer,1024);
										AddPart(fname,savereq->fr_File,1024);
										ami_update_pointer(twin->win,GUI_POINTER_WAIT);
										if(twin->type == AMI_TREE_HISTORY)
											history_global_export(fname);
										else if(twin->type == AMI_TREE_HOTLIST)
											hotlist_export(fname);
										ami_update_pointer(twin->win,GUI_POINTER_DEFAULT);
									}
								break;

								case 2: // expand
									switch(subnum)
									{
										case 0: // all
											switch(twin->type)
											{
												case AMI_TREE_HISTORY:
													history_global_expand_all();
												break;
												case AMI_TREE_COOKIES:
													cookies_expand_all();
												break;
												case AMI_TREE_HOTLIST:
													hotlist_expand_all();
												break;
											}
										break;

										case 1: // lev 1
											switch(twin->type)
											{
												case AMI_TREE_HISTORY:
													history_global_expand_directories();
												break;
												case AMI_TREE_COOKIES:
													cookies_expand_domains();
												break;
												case AMI_TREE_HOTLIST:
													hotlist_expand_directories();
												break;
											}
										break;

										case 2: // lev 2
											switch(twin->type)
											{
												case AMI_TREE_HISTORY:
													history_global_expand_addresses();
												break;
												case AMI_TREE_COOKIES:
													cookies_expand_cookies();
												break;
												case AMI_TREE_HOTLIST:
													hotlist_expand_addresses();
												break;
											}
										break;
									}
								break;

								case 3: // collapse
									switch(subnum)
									{
										case 0: // all
											switch(twin->type)
											{
												case AMI_TREE_HISTORY:
													history_global_collapse_all();
												break;
												case AMI_TREE_COOKIES:
													cookies_collapse_all();
												break;
												case AMI_TREE_HOTLIST:
													hotlist_collapse_all();
												break;
											}
										break;

										case 1: // lev 1
											switch(twin->type)
											{
												case AMI_TREE_HISTORY:
													history_global_collapse_directories();
												break;
												case AMI_TREE_COOKIES:
													cookies_collapse_domains();
												break;
												case AMI_TREE_HOTLIST:
													hotlist_collapse_directories();
												break;
											}
										break;

										case 2: // lev 2
											switch(twin->type)
											{
												case AMI_TREE_HISTORY:
													history_global_collapse_addresses();
												break;
												case AMI_TREE_COOKIES:
													cookies_collapse_cookies();
												break;
												case AMI_TREE_HOTLIST:
													hotlist_collapse_addresses();
												break;
											}
										break;
									}
								break;

								case 5: // snapshot
									switch(twin->type)
									{
										case AMI_TREE_HISTORY:
											option_history_window_ypos = twin->win->TopEdge;
											option_history_window_xpos = twin->win->LeftEdge;
											option_history_window_xsize = twin->win->Width;
											option_history_window_ysize = twin->win->Height;
										break;
										case AMI_TREE_COOKIES:
											option_cookies_window_ypos = twin->win->TopEdge;
											option_cookies_window_xpos = twin->win->LeftEdge;
											option_cookies_window_xsize = twin->win->Width;
											option_cookies_window_ysize = twin->win->Height;
										break;
										case AMI_TREE_HOTLIST:
											option_hotlist_window_ypos = twin->win->TopEdge;
											option_hotlist_window_xpos = twin->win->LeftEdge;
											option_hotlist_window_xsize = twin->win->Width;
											option_hotlist_window_ysize = twin->win->Height;
										break;
									}
								break;

								case 7: // close
									ami_tree_close(twin);
									return TRUE;
								break;
							}
						break;

						case 1: // edit
							switch(itemnum)
							{
								case 0: // delete
									switch(twin->type)
									{
										case AMI_TREE_HISTORY:
											history_global_delete_selected();
										break;
										case AMI_TREE_COOKIES:
											cookies_delete_selected();
										break;
										case AMI_TREE_HOTLIST:
											hotlist_delete_selected();
										break;
									}
									ami_tree_update_buttons(twin);
								break;

								case 2: // select all
									switch(twin->type)
									{
										case AMI_TREE_HISTORY:
											history_global_select_all();
										break;
										case AMI_TREE_COOKIES:
											cookies_select_all();
										break;
										case AMI_TREE_HOTLIST:
											hotlist_select_all();
										break;
									}
									ami_tree_update_buttons(twin);
								break;

								case 3: // clear
									switch(twin->type)
									{
										case AMI_TREE_HISTORY:
											history_global_clear_selection();
										break;
										case AMI_TREE_COOKIES:
											cookies_clear_selection();
										break;
										case AMI_TREE_HOTLIST:
											hotlist_clear_selection();
										break;
									}
									ami_tree_update_buttons(twin);
								break;
							}
						break;
					}

					if(win_destroyed) break;
					code = item->NextSelect;
				}
			break;

			case WMHI_NEWSIZE:
				ami_tree_draw(twin);
			break;

			case WMHI_CLOSEWINDOW:
				if(twin->type == AMI_TREE_SSLCERT)
					sslcert_reject(twin->ssl_data);
				ami_tree_close(twin);
				return TRUE;
			break;
		}
	}

	if(drag_x_move || drag_y_move)
		ami_tree_scroll(twin, drag_x_move, drag_y_move);

	return FALSE;
}

void ami_tree_draw(struct treeview_window *twin)
{
	struct IBox *bbox;
	int x, y;

	if(!twin) return;

	GetAttr(SCROLLER_Top, twin->objects[OID_HSCROLL], (ULONG *)&x);
	GetAttr(SCROLLER_Top, twin->objects[OID_VSCROLL], (ULONG *)&y);
	GetAttr(SPACE_AreaBox,twin->gadgets[GID_BROWSER],(ULONG *)&bbox);

	ami_tree_redraw_request(x, y, bbox->Width, bbox->Height, twin);
}

void ami_tree_redraw_request(int x, int y, int width, int height, void *data)
{
	struct treeview_window *twin = data;
	struct IBox *bbox;
	int pos_x, pos_y;

	if(!twin->win) return;
//	if(tree_get_redraw(twin->tree) == false) return;

	ami_update_pointer(twin->win, GUI_POINTER_WAIT);
	glob = &twin->globals;

	GetAttr(SPACE_AreaBox,twin->gadgets[GID_BROWSER],(ULONG *)&bbox);
	GetAttr(SCROLLER_Top, twin->objects[OID_HSCROLL], (ULONG *)&pos_x);
	GetAttr(SCROLLER_Top, twin->objects[OID_VSCROLL], (ULONG *)&pos_y);

	if(x - pos_x + width > bbox->Width) width = bbox->Width - (x - pos_x);
	if(y - pos_y + height > bbox->Height) height = bbox->Height - (y - pos_y);

	ami_clg(0xffffffff);

	tree_draw(twin->tree, -pos_x, -pos_y, x, y, width, height);

	BltBitMapRastPort(twin->globals.bm, x - pos_x, y - pos_y, twin->win->RPort,
		bbox->Left + x - pos_x, bbox->Top + y - pos_y, width, height, 0x0C0);

	ami_update_pointer(twin->win, GUI_POINTER_DEFAULT);
	glob = &browserglob;
}
