/*
GraphSlick (c) Elias Bachaalany
-------------------------------------

Plugin module

This module is responsible for handling IDA plugin code

History
--------

10/15/2013 - eliasb             - First version
10/21/2013 - eliasb             - Working chooser / graph renderer
10/22/2013 - eliasb             - Version with working selection of NodeDefLists and GroupDefs
                                - Now the graph-view closes when the panel closes
                                - Factored out functions from the grdata_t class
                                - Wrote initial combine nodes algorithm
10/23/2013 - eliasb             - Polished and completed the combine algorithm
                                - Factored out many code into various modules
10/24/2013 - eliasb             - Added proper coloring on selection (via colorgen class)
                                - Factored out many code into various modules
                 								- Fixed crash on re-opening the plugin chooser
10/25/2013 - eliasb             - Refactored the code further
								                - Renamed from grdata_t to a full graphview class (gsgraphview_t)
                                - Devised the graph context menu system
								                - Added clear/toggle selection mode
								                - Added graph view mode functionality
								                - Dock the gsgraphview next to IDA-View
                                - Factored out get_color_anyway() to the colorgen module
                                - Proper support for node selection/coloring in single and combined mode
                                - Change naming from chooser nodes to chooser line
10/28/2013 - eliasb             - Separated highlight and selection
                                - Added support for 'user hint' on the node graph
                                - Added 'Load bbgroup' support
                                - Added quick selection support (no UI for it though)
                                - Added reload input file functionality
10/29/2013 - eliasb             - Added 'Highlight similar nodes' placeholder code and UI
                                - Refactored chooser population code into 'populate_chooser_lines()'
                                - Speed optimization: generate flowchart once in the chooser and pass it to other functions								
                                - Added find_and_highlight_nodes() to the graph								
10/30/2013 - eliasb             - Adapted to new class names
                                - Added to-do placeholder for parameters that could be passed as options
10/31/2013 - eliasb             - Get rid of the 'additive' parameter in set_highlighted_nodes
                                - renamed set_highlighted_nodes() to highlight_nodes(). it also checks whether to highlight or not
                                  (thus the caller does not have to do redundant highlight/lazy checks)
                                - Introduced the options class and parameterized all relevant function calls
                                - Added 'refresh view' and 'show options' context menus								
                                - Introduced get_ng_id() helper function to resolve node ids automatically based on current view mode
                                - Fixed wrong node id resolution upon double click on chooser lines								
11/01/2013 - eliasb             - Introduced redo_layout() to actually refresh and make the layout
                                - refresh_view() is a placeholder for a hack that will cause a screen repaint
                                - renamed 'lazy_sh_mode' to 'manual_refresh_mode'
                                - added a second chooser column to show EA of first ND in the NG
                                - got rid of 'refresh view' context menu item -> redundant with IDA's layout graph
11/04/2013 - eliasb             -  added 'debug' option
                                - "Edit SG" is no more dynamic. It seems it is not possible to add menu item inside the grcallback / refresh cb
                                - Added "Combine nodes" functionality
                                - refactored graph mode view switch into functions
                                - Added get_ng_from_ngid(ngid) utility function to do reverse ng2nid lookup
                                - Added  ngid_to_sg(ngid) utility function to do ngid to containing SG lookup
                                - Temporarlily added 'refresh_parent()' to refresh the chooser. Thinking to introduce a chooser interface to allow communication between gsgv and gsch
*/

#pragma warning(disable: 4018 4800)

#include <ida.hpp>
#include <idp.hpp>
#include <graph.hpp>
#include <loader.hpp>
#include <kernwin.hpp>
#include "groupman.h"
#include "util.h"
#include "algo.hpp"
#include "colorgen.h"

//--------------------------------------------------------------------------
#define MY_TABSTR "    "

#define STR_GS_MSG "GS: "

//--------------------------------------------------------------------------
static const char STR_CANNOT_BUILD_F_FC[] = "Cannot build function flowchart!";
static const char STR_GS_PANEL[]          = "Graph Slick - Panel";
static const char STR_GS_VIEW[]           = "Graph Slick - View";
static const char STR_OUTWIN_TITLE[]      = "Output window";
static const char STR_IDAVIEWA_TITLE[]    = "IDA View-A";
static const char STR_SEARCH_PROMPT[]     = "Please enter search string";
static const char STR_DUMMY_SG_NAME[]     = "No name";

//--------------------------------------------------------------------------
typedef std::map<int, bgcolor_t> ncolormap_t;

const bgcolor_t NODE_SEL_COLOR = 0x7C75AD;

//--------------------------------------------------------------------------
enum gvrefresh_modes_e
{
  gvrfm_soft,
  gvrfm_single_mode,
  gvrfm_combined_mode,
};

//--------------------------------------------------------------------------
#define DECL_CG \
  colorgen_t cg; \
  cg.L_INT = -15


//--------------------------------------------------------------------------
/**
* @brief GraphSlick options handling class
*/
struct gsoptions_t
{
  /**
  * @brief Append node id to the node text
  */
  bool append_node_id;

  /**
  * @brief Manual refresh view on selection/highlight
  */
  bool manual_refresh_mode;

  /**
  * @brief Highlight synthetic nodes 
  */
  bool highlight_syntethic_nodes;

  /**
  * @brief Should the options dialog be shown the next time?
  */
  bool show_options_dialog_next_time;

  /**
  * @brief If the group name is one line then add a few more lines
  *        to make it look bigger
  */
  bool enlarge_group_name;

  /**
  * @brief Display debug messages
  */
  bool debug;

  /**
  * @brief GraphSlick start up view mode
  */
  gvrefresh_modes_e start_view_mode;

  //--------------------------------------------------------------------------

  /**
  * @brief Show the options dialog
  */
  void show_dialog()
  {
    // TODO: askusingform
  }

  /**
  * @brief Load options from the current database
  */
  void load_options()
  {
    //TODO: load from netnode
    manual_refresh_mode = true;
    append_node_id = false;
    highlight_syntethic_nodes = false;
    show_options_dialog_next_time = true;
    enlarge_group_name = true;
    start_view_mode = gvrfm_combined_mode; // gvrfm_single_mode;
    debug = true;
  }

  /**
  * @brief Save options to the current database
  */
  void save_options()
  {
    // TODO: save to netnode
  }
};

//--------------------------------------------------------------------------
/**
* @brief Graph data/context
*/
class gsgraphview_t
{
public:
  /**
  * @brief Currently selected node
  */
  int cur_node;

  /**
  * @brief Pointer to the graph viewer
  */
  graph_viewer_t  *gv;

  /**
  * @brief Pointer to the form hosting the graph viewer
  */
  TForm *form;

  /**
  * @brief Pointer to the associated group manager class
  */
  groupman_t *gm;

  /**
  * @brief GraphSlick options
  */
  gsoptions_t *options;

private:
  struct menucbctx_t
  {
    gsgraphview_t *gsgv;
    qstring name;
  };
  typedef std::map<int, menucbctx_t> idmenucbtx_t;
  static idmenucbtx_t menu_ids;

  gnodemap_t node_map;
  ng2nid_t ng2id;
  qflow_chart_t *func_fc;
  gvrefresh_modes_e refresh_mode, cur_view_mode;

  gsgraphview_t **parent_ref;

  /**
  * @brief View mode menu items
  */
  int idm_single_view_mode, idm_combined_view_mode;

  int idm_clear_sel, idm_clear_highlight;
  int idm_set_sel_mode;

  int idm_edit_sg_desc;

  int idm_test;
  int idm_highlight_similar, idm_find_highlight;

  int idm_combine_ngs;

  int idm_show_options;

  bool in_sel_mode;

  ncolormap_t     highlighted_nodes;
  ncolormap_t     selected_nodes;

  /**
  * @brief Static menu item dispatcher
  */
  static bool idaapi s_menu_item_callback(void *ud)
  {
    int id = (int)ud;
    idmenucbtx_t::iterator it = menu_ids.find(id);
    if (it == menu_ids.end())
      return false;

    it->second.gsgv->on_menu(id);

    return true;
  }

  /**
  * @brief Menu items handler
  */
  void on_menu(int menu_id)
  {
    //
    // Clear selection
    //
    if (menu_id == idm_clear_sel)
    {
      clear_selection(options->manual_refresh_mode);
    }
    //
    // Clear highlighted nodes
    //
    else if (menu_id == idm_clear_highlight)
    {
      clear_highlighting(options->manual_refresh_mode);
    }
    //
    // Selection mode change
    //
    else if (menu_id == idm_set_sel_mode)
    {
      // Toggle selection mode
      set_sel_mode(!in_sel_mode);
    }
    //
    // Switch to single view mode
    //
    else if (menu_id == idm_single_view_mode)
    {
      redo_layout(gvrfm_single_mode);
    }
    //
    // Switch to combined view mode
    //
    else if (menu_id == idm_combined_view_mode)
    {
      redo_layout(gvrfm_combined_mode);
    }
    //
    // Show the options dialog
    //
    else if (menu_id == idm_show_options)
    {
      options->show_dialog();
    }
    //
    // Highlight similar node group
    //
    else if (menu_id == idm_highlight_similar)
    {
      highlight_similar_selection(options->manual_refresh_mode);
    }
    //
    // Find and highlight supergroup
    //
    else if (menu_id == idm_find_highlight)
    {
      find_and_highlight_nodes(options->manual_refresh_mode);
    }
    //
    // Edit supergroup description
    //
    else if (menu_id == idm_edit_sg_desc)
    {
      // Check the view mode and selection
      if (   cur_view_mode != gvrfm_combined_mode 
          || cur_node == -1)
      {
        msg(STR_GS_MSG "Incorrect view mode or no nodes are selected\n");
        return;
      }

      psupergroup_t sg = ngid_to_sg(cur_node);
      if (sg == NULL)
        return;

      if (edit_sg_description(sg))
      {
        // Refresh the chooser
        refresh_parent();
      }
    }
    //
    // Test: interactive groupping
    //
    else if (menu_id == idm_combine_ngs)
    {
      //
      if (cur_view_mode != gvrfm_combined_mode)
      {
        msg(STR_GS_MSG "Groupping is only available in combined mode\n");
        return;
      }

      if (selected_nodes.size() <= 1)
      {
        msg(STR_GS_MSG "Not enough selected nodes\n");
        return;
      }

      //
      // Make a nodegroup list off the selection
      //
      nodegroup_list_t ngl;
      for (ncolormap_t::iterator it = selected_nodes.begin();
           it != selected_nodes.end();
           ++it)
      {
        // Get the other selected NG
        pnodegroup_t ng = get_ng_from_ngid(it->first);
        ngl.push_back(ng);
      }

      // Combine the selected NGLs
      gm->combine_ngl(&ngl);

      // Refresh the chooser
      refresh_parent();

      redo_layout(cur_view_mode);
    }
    //
    // Test: interactive groupping
    //
    else if (menu_id == idm_test)
    {
    }
  }

#ifdef _DEBUG
  /**
  * @brief 
  * @param
  * @return
  */
  void _DUMP_NG(const char *str, pnodegroup_t ng)
  {
    for (nodegroup_t::iterator it=ng->begin();
         it != ng->end();
         ++it)
    {
      pnodedef_t nd = *it;
      msg("%s: p=%p id=%d s=%a e=%a\n", str, nd, nd->nid, nd->start, nd->end);
    }
  }
#endif

  /**
  * @brief Return node data
  */
  gnode_t *get_node(int nid)
  {
    return node_map.get(nid);
  }

  /**
  * @brief Static graph callback
  */
  static int idaapi _gr_callback(
      void *ud, 
      int code, va_list va)
  {
    return ((gsgraphview_t *)ud)->gr_callback(code, va);
  }

  /**
  * @brief Graph callback
  */
  int idaapi gr_callback(
        int code,
        va_list va)
  {
    int result = 0;
    switch (code)
    {
      //
      // graph is being clicked
      //
      case grcode_clicked:
      {
        va_arg(va, graph_viewer_t *);
        selection_item_t *item1 = va_arg(va, selection_item_t *);
        va_arg(va, graph_item_t *);
        if (in_sel_mode && item1 != NULL && item1->is_node)
        {
          toggle_select_node(
            item1->node, 
            options->manual_refresh_mode);
        }

        // don't ignore the click
        result = 0;
        break;
      }

      //
      // a new graph node became the current node
      //
      case grcode_changed_current:
      {
        va_arg(va, graph_viewer_t *);

        // Remember the current node
        cur_node = va_argi(va, int);

        break;
      }

      //
      // A group is being created
      //
      case grcode_creating_group:
      {
        //mutable_graph_t *mg = va_arg(va, mutable_graph_t *);
        //intset_t *nodes = va_arg(va, intset_t *);

        //msg("grcode_creating_group(%p):", mg);
        //for (intset_t::iterator it=nodes->begin();
        //     it != nodes->end();
        //     ++it)
        //{
        //  msg("%d,", *it);
        //}
        //msg("\n");
        //result = 1;
        // out: 0-ok, 1-forbid group creation
        break;
      }

      //
      // A group is being deleted
      //
      case grcode_deleting_group:
      {
        // in:  mutable_graph_t *g
        //      int old_group
        // out: 0-ok, 1-forbid group deletion
        break;
      }

      //
      // New graph has been set
      //
      case grcode_changed_graph:
      {
        // in: mutable_graph_t *g
        mutable_graph_t *mg = va_arg(va, mutable_graph_t *);
        // out: must return 0
        //msg("grcode_changed_graph: %p\n", mg);
        break;
      }

      //
      // Redraw the graph
      //
      case grcode_user_refresh:
      {
        mutable_graph_t *mg = va_arg(va, mutable_graph_t *);
        if (node_map.empty() || refresh_mode != gvrfm_soft)
        {
          // Clear previous graph node data
          mg->clear();
          reset_states();
		  
          // Remember the current graph mode
          // NOTE: we remember the state only if not 'soft'. 
          //       Otherwise it will screw up all the logic that rely on its value
          cur_view_mode = refresh_mode;
		  
          // Switch to the desired mode
          if (refresh_mode == gvrfm_single_mode)
            switch_to_single_view_mode(mg);
          else if (refresh_mode == gvrfm_combined_mode)
            switch_to_combined_view_mode(mg);
        }
        result = 1;
        break;
      }

      //
      // Retrieve text and background color for the user-defined graph node
      //
      case grcode_user_text:    
      {
        va_arg(va, mutable_graph_t *);
        int node           = va_arg(va, int);
        const char **text  = va_arg(va, const char **);
        bgcolor_t *bgcolor = va_arg(va, bgcolor_t *);

        // Retrieve the node text
        gnode_t *gnode = get_node(node);
        if (gnode == NULL)
        {
          result = 0;
          break;
        }

        *text = gnode->text.c_str();

        // Caller requested a bgcolor?
        if (bgcolor != NULL) do
        {
          // Selection has priority over highlight
          ncolormap_t::iterator psel = selected_nodes.find(node);
          if (psel == selected_nodes.end())
          {
            psel = highlighted_nodes.find(node);
            if (psel == highlighted_nodes.end())
              break;
          }
          // Pass the color
          *bgcolor = psel->second;
        } while (false);

        result = 1;
        break;
      }

      //
      // retrieve hint for the user-defined graph
      //
      case grcode_user_hint:
      {
        va_arg(va, mutable_graph_t *);
        int mousenode = va_arg(va, int);
        va_arg(va, int); // mouseedge_src
        va_arg(va, int); // mouseedge_dst
        char **hint = va_arg(va, char **);

        // Get node data, aim for 'hint' field then 'text'
        gnode_t *node_data;
        if (     mousenode != -1 
             && (node_data = get_node(mousenode)) != NULL )
        {
          qstring *s = &node_data->hint;
          if (s->empty())
            s = &node_data->text;

          // 'hint' must be allocated by qalloc() or qstrdup()
          *hint = qstrdup(s->c_str());

          // out: 0-use default hint, 1-use proposed hint
          result = 1;
        }
        break;
      }

      //
      // The graph is being destroyed
      //
      case grcode_destroyed:
      {
        gv = NULL;
        form = NULL;

        // Parent ref set?
        if (parent_ref != NULL)
        {
          // Clear this variable in the parent
          *parent_ref = NULL;
        }

        delete this;
        break;
      }
    }
    return result;
  }

  /**
  * @brief Convert a node group id to a nodegroup instance
  */
  pnodegroup_t get_ng_from_ngid(int ngid)
  {
    //TODO: PERFORMANCE: make a lookup table if needed
    for (ng2nid_t::iterator it=ng2id.begin();
         it != ng2id.end();
         ++it)
    {
      if (it->second == ngid)
        return it->first;
    }
    return NULL;
  }

  /**
  * @brief Set the selection mode
  */
  void set_sel_mode(bool sel_mode)
  {
    // Previous mode was set?
    if (idm_set_sel_mode != -1)
    {
      // Delete previous menu item
      del_menu(idm_set_sel_mode);
    }

    const char *labels[] = 
    {
      "Start selection mode",
      "End selection mode"
    };
    const char *label = labels[sel_mode ? 1 : 0];

    idm_set_sel_mode = add_menu(label, "S");

    in_sel_mode = sel_mode;
    msg(STR_GS_MSG "Trigger again to '%s'\n", label);
  }

public:

  /**
  * @brief Refresh the current screen and the visible nodes (not the layout)
  */
  void refresh_view()
  {
    refresh_mode = gvrfm_single_mode;
    //TODO: HACK: trigger a window to show/hide so IDA repaints the nodes
  }

  /**
  * @brief Set refresh mode and issue a refresh
  */
  void redo_layout(gvrefresh_modes_e rm)
  {
    refresh_mode = rm;
    refresh_viewer(gv);
  }

  /**
  * @brief Edit the description of a super group
  */
  bool edit_sg_description(psupergroup_t sg)
  {
    const char *desc = askstr(
      HIST_CMT,
      sg->get_display_name(STR_DUMMY_SG_NAME),
      "Please enter new description");

    if (desc == NULL)
      return false;

    // Adjust the name
    sg->name = desc;

    // From the super group, get all individual node groups
    for (nodegroup_list_t::iterator it=sg->groups.begin();
         it != sg->groups.end();
         ++it)
    {
      // Get the group node id from the node group reference
      pnodegroup_t ng = *it;
      int ngid = get_ng_id(ng);
      if (ngid == -1)
        continue;

      gnode_t *gnode = get_node(ngid);
      if (gnode == NULL)
        continue;;

      // Update the node display text
      // TODO: PERFORMANCE: can you have gnode link to a groupman related structure and pull its
      //                    text dynamically?
      gnode->text = sg->get_display_name();
    }

    if (!options->manual_refresh_mode)
      refresh_view();

    return true;
  }

  /**
  * @brief Highlights a group
  */
  bool highlight_nodes(
      pnodegroup_t ng, 
      bgcolor_t clr,
      bool delay_refresh)
  {
    intset_t newly_colored;

    // Combined mode?
    if (cur_view_mode == gvrfm_combined_mode)
    {
      int gr_nid = get_ng_id(ng);
      if (gr_nid == -1)
        return false;

      if (delay_refresh)
        newly_colored.insert(gr_nid);

      highlighted_nodes[gr_nid] = clr;
    }
    // Single view mode?
    else if (cur_view_mode == gvrfm_single_mode)
    {
      // Add each node in the definition to the selection
      for (nodegroup_t::iterator it = ng->begin();
           it != ng->end();
           ++it)
      {
        int nid = (*it)->nid;

        if (delay_refresh)
          newly_colored.insert(nid);

        highlighted_nodes[nid] = clr;
      }
    }
    // Unknown mode
    else
    {
      return false;
    }

    // In delayed refresh mode, just print what we plan to highlight
    if (delay_refresh)
    {
      // Just print
      msg(STR_GS_MSG "Lazy highlight( ");
      size_t t = newly_colored.size();
      for (intset_t::iterator it=newly_colored.begin();
            it != newly_colored.end();
            ++it)
      {
        int nid = *it;
        if (cur_view_mode == gvrfm_single_mode)
        {
          pnodedef_t nd = (*gm->get_nds())[nid];
          if (nd == NULL)
            continue;

          msg("%d : %a : %a ", nd->nid, nd->start, nd->end);
          if (--t > 0)
            msg(", ");
        }
        else
        {
          msg("%d ", nid);
        }
      }
      msg(")\n");
    }
    // Refresh immediately
    else
    {
      refresh_view();
    }
    return true;
  }

  /**
  * @brief Highlight a nodegroup list
  */
  void highlight_nodes(
          pnodegroup_list_t ngl, 
          colorgen_t &cg,
          bool delay_refresh)
  {
    // Use one color for all the different group defs
    colorvargen_t cv;
    cg.get_colorvar(cv);

    for (nodegroup_list_t::iterator it=ngl->begin();
         it != ngl->end();
         ++it)
    {
      // Use a new color variant for each node group
      bgcolor_t clr = cg.get_color_anyway(cv);
      pnodegroup_t ng = *it;

      // Always call with delayed refresh mode in the inner loop
      highlight_nodes(
          ng, 
          clr,
          true);
    }

    // Since we called with delayed refresh mode, now see if refresh is needed
    if (!delay_refresh)
      refresh_view();
  }

  /**
  * @brief Selects all set of super groups
  */
  void highlight_nodes(
    psupergroup_listp_t groups,
    colorgen_t &cg,
    bool delay_refresh)
  {
    colorvargen_t cv;
    for (supergroup_listp_t::iterator it=groups->begin();
         it != groups->end();
         ++it)
    {
      // Get the super group
      psupergroup_t sg = *it;

      // - Super group is synthetic?
      // - User does not want us to color such sgs?
      if (    sg->is_synthetic 
           && !options->highlight_syntethic_nodes)
      {
        // Don't highlight syntethic super groups
        continue;
      }

      // Assign a new color variant for each group
      cg.get_colorvar(cv);
      for (nodegroup_list_t::iterator it=sg->groups.begin();
           it != sg->groups.end();
           ++it)
      {
        // Use a new color variant for each group
        bgcolor_t clr = cg.get_color_anyway(cv);
        pnodegroup_t ng = *it;

        // Always call with lazy mode in the inner loop
        highlight_nodes(
            ng, 
            clr, 
            true);
      }
    }

    // Since we were called with delayed refresh mode, now see if refresh is needed
    if (!delay_refresh)
      refresh_view();
  }


  /**
  * @brief Set the parent ref variable
  */
  inline void set_parentref(gsgraphview_t **pr)
  {
    parent_ref = pr;
  }

  /**
  * @brief Clear the selection
  */
  void clear_selection(bool delay_refresh)
  {
    selected_nodes.clear();
    if (!delay_refresh)
      refresh_view();
  }

  /**
  * @brief Clear the highlighted nodes
  */
  void clear_highlighting(bool delay_refresh)
  {
    highlighted_nodes.clear();
    if (!delay_refresh)
      refresh_view();
  }

  /**
  * @brief Creates and shows the graph
  */
  static gsgraphview_t *show_graph(
    qflow_chart_t *func_fc,
    groupman_t *gm,
    gsoptions_t *options)
  {
    // Loop twice: 
    // - (1) Create the graph and exit or close it if it was there 
    // - (2) Re create graph due to last step
    for (int init=0; init<2; init++)
    {
      HWND hwnd = NULL;
      TForm *form = create_tform(STR_GS_VIEW, &hwnd);
      if (hwnd != NULL)
      {
        // get a unique graph id
        netnode id;
        qstring title;
        title.sprnt("$ GS %s", func_fc->title.c_str());
        id.create(title.c_str());

        // Create a graph object
        gsgraphview_t *gsgv = new gsgraphview_t(func_fc, options);

        // Assign the groupmanager instance
        gsgv->gm = gm;

        // Create the graph control
        graph_viewer_t *gv = create_graph_viewer(
          form,  
          id, 
          gsgv->_gr_callback, 
          gsgv, 
          0);

        open_tform(form, FORM_TAB|FORM_MENU|FORM_QWIDGET);
        if (gv != NULL)
          gsgv->init(gv, form);

        return gsgv;
      }
      else
      {
        close_tform(form, 0);
      }
    }
    return NULL;
  }

  /**
  * @brief Add a context menu to the graphview
  */
  int add_menu(
    const char *name,
    const char *hotkey = NULL)
  {
    // Static ID for all menu item IDs
    static int id = 0;

    // Is this a separator menu item?
    bool is_sep = name[0] == '-' && name[1] == '\0';

    // Only remember this item if it was not a separator
    if (!is_sep)
    {
      ++id;

      // Create a menu context
      menucbctx_t ctx;
      ctx.gsgv = this;
      ctx.name = name;

      menu_ids[id] = ctx;
    }

    bool ok = viewer_add_menu_item(
      gv,
      name,
      is_sep ? NULL : s_menu_item_callback,
      (void *)id,
      hotkey,
      0);

    // Ignore return value for separator
    if (is_sep)
      return -1;

    if (!ok)
      menu_ids.erase(id);

    return ok ? id : -1;
  }

  /**
  * @brief Delete a context menu item
  */
  void del_menu(int menu_id)
  {
    if (menu_id == -1)
      return;

    idmenucbtx_t::iterator it = menu_ids.find(menu_id);
    if (it == menu_ids.end())
      return;

    viewer_del_menu_item(
      it->second.gsgv->gv, 
      it->second.name.c_str());

    menu_ids.erase(menu_id);
  }

  /**
  * @brief Constructor
  */
  gsgraphview_t(qflow_chart_t *func_fc, gsoptions_t *options): func_fc(func_fc), options(options)
  {
    gv = NULL;
    form = NULL;
    refresh_mode = options->start_view_mode;
    set_parentref(NULL);

    in_sel_mode = false;
    cur_node = -1;
    idm_set_sel_mode = -1;
    idm_edit_sg_desc = -1;
  }

  /**
  * @brief Initialize the graph view
  */
  void init(graph_viewer_t *gv, TForm *form)
  {
    this->gv = gv;
    this->form = form;
    viewer_fit_window(gv);
    viewer_center_on(gv, 0);

    //
    // Add the context menu items
    //

    add_menu("-");
    idm_show_options        = add_menu("Show options",                 "O");

    // Highlighting / selection actions
    add_menu("-");
    idm_clear_sel           = add_menu("Clear selection",              "D");
    idm_clear_highlight     = add_menu("Clear highlighting",           "H");

    // Switch view mode actions
    add_menu("-");
    idm_single_view_mode    = add_menu("Switch to ungroupped view",    "U");
    idm_combined_view_mode  = add_menu("Switch to groupped view",      "G");

    // Experimental actions
    add_menu("-");
    idm_test                = add_menu("Test",                         "Q");

    // Searching actions
    add_menu("-");
    idm_highlight_similar  = add_menu("Highlight similar nodes",       "M");
    idm_find_highlight     = add_menu("Find group",                    "F");

    // Groupping actions

    idm_combine_ngs        = add_menu("Combine nodes",                 "C");

    // Add the edit group description menu
    idm_edit_sg_desc       = add_menu("Edit group description");

    //
    // Dynamic menu items
    //
    
    // Set initial selection mode
    add_menu("-");
    set_sel_mode(in_sel_mode);
  }

  /**
  * @brief Toggle node selection
  */
  void toggle_select_node(
          int cur_node, 
          bool delay_refresh)
  {
    ncolormap_t::iterator p = selected_nodes.find(cur_node);
    if (p == selected_nodes.end())
      selected_nodes[cur_node] = NODE_SEL_COLOR;
    else
      selected_nodes.erase(p);

    // With quick selection mode, just display a message and don't force a refresh
    if (delay_refresh)
    {
      msg(STR_GS_MSG "Selected %d\n", cur_node);
    }
    else
    {
      // Refresh the graph to reflect selection
      refresh_view();
    }
  }

  /**
  * @brief Highlight nodes similar to the selection
  */
  void highlight_similar_selection(bool delay_refresh)
  {
    if (selected_nodes.empty())
      return;

    //TODO: highlight the similar selection
    msg(STR_GS_MSG "Not implemented yet!\n");
  }

  /**
  * @brief Find and highlights nodes
  */
  void find_and_highlight_nodes(bool delay_refresh)
  {
    static char last_pattern[MAXSTR] = {0};

    const char *pattern = askstr(HIST_SRCH, last_pattern, STR_SEARCH_PROMPT);
    if (pattern == NULL)
      return;

    // Remember last search
    qstrncpy(
        last_pattern, 
        pattern, 
        sizeof(last_pattern));

    DECL_CG;

    clear_highlighting(true);

    pnodegroup_list_t groups = NULL;

    // Walk all the groups
    psupergroup_listp_t sgroups = gm->get_path_sgl();
    for (supergroup_listp_t::iterator it=sgroups->begin();
         it != sgroups->end();
         ++it)
    {
      psupergroup_t sg = *it;
      if (    stristr(sg->name.c_str(), pattern) != NULL
           || stristr(sg->id.c_str(), pattern) != NULL )
      {
        groups = &sg->groups;
        highlight_nodes(
          groups, 
          cg, 
          true);
      }
    }

    // Refresh graph if at least there is one match
    if (!delay_refresh)
	{
      refresh_view();
      if (groups == NULL)
        return;
	}

    int nid;
    pnodegroup_t ng = groups->get_first_ng();
    nid = ng == NULL ? -1 : get_ng_id(ng);

    if (nid != -1)
    {
      jump_to_node(
        gv, 
        nid);
    }
  }

  /**
  * @brief Return supergroup for which a nodegroup_id belongs to
  */
  psupergroup_t ngid_to_sg(int ngid)
  {
    // The current node is a node group id
    // Convert ngid to a node id
    pnodegroup_t ng = get_ng_from_ngid(cur_node);
    if (ng == NULL)
      return NULL;

    pnodedef_t nd = ng->get_first_node();
    if (nd == NULL)
      return NULL;

    nodeloc_t *loc = gm->find_nodeid_loc(nd->nid);
    if (loc == NULL)
      return NULL;
    else
      return loc->sg;
  }

  /**
  * @brief Return a node id corresponding to the given node group. 
  *        The current view mode is respected
  */
  inline int get_ng_id(pnodegroup_t ng)
  {
    if (ng != NULL)
    {
      if (cur_view_mode == gvrfm_combined_mode)
      {
	    // Get the nodegroup id from the map
        return ng2id.get_ng_id(ng);
      }
      else if (cur_view_mode == gvrfm_single_mode)
      {
	    // Just get the node id of the first node definition in the node group
        pnodedef_t nd = ng->get_first_node();
        return nd == NULL ? -1 : nd->nid;
      }
    }
    if (options->debug)
      msg(STR_GS_MSG "Could not find gr_nid for %p\n", ng);
    return -1;
  }

  /**
  * @brief Resets state variables upon view mode change
  */
  void reset_states()
  {
    // Clear node information
    node_map.clear();
    ng2id.clear();

    // Clear highlight / selected
    highlighted_nodes.clear();
    selected_nodes.clear();

    // No node is selected
    cur_node = -1;
  }

  /**
  * @brief Switch to combined view mode
  */
  void switch_to_single_view_mode(mutable_graph_t *mg)
  {
    msg(STR_GS_MSG "Switching to single mode view...");
    func_to_mgraph(
      BADADDR, 
      mg, 
      node_map, 
      func_fc,
      options->append_node_id);
    msg("done\n");
  }

  /**
  * @brief Switch to combined view mode
  */
  void switch_to_combined_view_mode(mutable_graph_t *mg)
  {
    msg(STR_GS_MSG "Switching to combined mode view...");
    fc_to_combined_mg(
      BADADDR, 
      gm, 
      node_map, 
      ng2id, 
      mg, 
      func_fc);

    msg("done\n");
  }

  /**
  * @brief Refreshes the parent control
  */
  void refresh_parent(bool populate_lines = false)
  {
    if (populate_lines)
    {
      //TODO: how to do it?
      //;!populate_chooser_lines();
    }
    refresh_chooser(STR_GS_PANEL);
  }
};
gsgraphview_t::idmenucbtx_t gsgraphview_t::menu_ids;

//--------------------------------------------------------------------------
/**
* @brief Types of lines in the chooser
*/
enum gsch_line_type_t
{
  chlt_gm  = 0,
  chlt_sg  = 1,
  chlt_ng  = 3,
};

//--------------------------------------------------------------------------
/**
* @brief Chooser line structure
*/
class gschooser_line_t
{
public:
  gsch_line_type_t type;
  groupman_t *gm;
  psupergroup_t sg;
  pnodegroup_t ng;
  pnodegroup_list_t ngl;

  /**
  * @brief Constructor
  */
  gschooser_line_t()
  {
    gm = NULL;
    sg = NULL;
    ng = NULL;
    ngl = NULL;
  }
};
typedef qvector<gschooser_line_t> chooser_lines_vec_t;

//--------------------------------------------------------------------------
/**
* @brief GraphSlick chooser class
*/
class gschooser_t
{
private:
  static gschooser_t *singleton;
  chooser_lines_vec_t ch_nodes;

  chooser_info_t chi;
  gsgraphview_t *gsgv;
  groupman_t *gm;
  qstring last_loaded_file;

  qflow_chart_t func_fc;
  gsoptions_t options;

  static uint32 idaapi s_sizer(void *obj)
  {
    return ((gschooser_t *)obj)->on_get_size();
  }

  static void idaapi s_getl(void *obj, uint32 n, char *const *arrptr)
  {
    ((gschooser_t *)obj)->on_get_line(n, arrptr);
  }

  static uint32 idaapi s_del(void *obj, uint32 n)
  {
    return ((gschooser_t *)obj)->on_delete(n);
  }

  static void idaapi s_ins(void *obj)
  {
    ((gschooser_t *)obj)->on_insert();
  }

  static void idaapi s_enter(void *obj, uint32 n)
  {
    ((gschooser_t *)obj)->on_enter(n);
  }

  static void idaapi s_refresh(void *obj)
  {
    ((gschooser_t *)obj)->on_refresh();
  }

  static void idaapi s_initializer(void *obj)
  {
    ((gschooser_t *)obj)->on_init();
  }

  static void idaapi s_destroyer(void *obj)
  {
    ((gschooser_t *)obj)->on_destroy();
  }

  static void idaapi s_edit(void *obj, uint32 n)
  {
    ((gschooser_t *)obj)->on_edit_line(n);
  }

  static void idaapi s_select(void *obj, const intvec_t &sel)
  {
    ((gschooser_t *)obj)->on_select(sel);
  }

  /**
  * @brief Delete the singleton instance if applicable
  */
  void delete_singleton()
  {
    //TODO: I don't like this singleton thing
    if ((chi.flags & CH_MODAL) != 0)
      return;

    delete singleton;
    singleton = NULL;
  }

  /**
  * @brief Handles instant node selection in the chooser
  */
  void on_select(const intvec_t &sel)
  {
    // Delegate this task to the 'enter' routine
    highlight_node(sel[0]-1);
  }

  /**
  * @brief Return the items count
  */
  uint32 on_get_size()
  {
    return ch_nodes.size();
  }

  /**
  * @brief Return chooser line description
  */
  void get_node_desc(
        gschooser_line_t *node, 
        qstring *out,
        int col = 1)
  {
    switch (node->type)
    {
      // Handle a group file node
      case chlt_gm:
      {
        if (col == 1)
          *out = qbasename(node->gm->get_source_file());
        break;
      }
      // Handle super groups
      case chlt_sg:
      {
        if (col == 1)
        {
          out->sprnt(MY_TABSTR "%s (%s) C(%d)", 
            node->sg->name.c_str(), 
            node->sg->id.c_str(),
            node->sg->gcount());
        }
        break;
      }
      // Handle a node definition list
      case chlt_ng:
      {
        pnodegroup_t groups = node->ng;

        if (col == 1)
        {
          size_t sz = groups->size();
          out->sprnt(MY_TABSTR MY_TABSTR "C(%d):(", sz);
          for (nodegroup_t::iterator it=groups->begin();
                it != groups->end();
                ++it)
          {
            pnodedef_t nd = *it;
            out->cat_sprnt("%d:%a:%a", nd->nid, nd->start, nd->end);
            if (--sz > 0)
              out->append(", ");
          }
          out->append(")");
        }
        else if (col == 2)
        {
          // Show the EA of the first node in this node group
          pnodedef_t nd = groups->get_first_node();
          if (nd != NULL)
            out->sprnt("%a", nd->start);
        }
        break;
      }
    }
  }

  /**
  * @brief On edit line
  */
  void on_edit_line(uint32 n)
  {
    if (   gsgv == NULL  
        || n > ch_nodes.size() 
        || !IS_SEL(n) )
    {
      return;
    }

    gschooser_line_t &chn = ch_nodes[n-1];
    if (chn.type != chlt_sg)
      return;

    gsgv->edit_sg_description(chn.sg);
  }

  /**
  * @brief Get textual representation of a given line
  */
  void on_get_line(
      uint32 n, 
      char *const *arrptr)
  {
    // Return the column name
    if (n == 0)
    {
      qstrncpy(arrptr[0], "Node", MAXSTR);
      return;
    }
    // Return description about a node
    else if (n >= 1)
    {
      --n;
      if (n >= ch_nodes.size())
        return;

      gschooser_line_t &cn = ch_nodes[n];

      qstring desc;
      get_node_desc(&cn, &desc, 1);
      qstrncpy(arrptr[0], desc.c_str(), MAXSTR);

      desc.qclear();
      get_node_desc(&cn, &desc, 2);
      qstrncpy(arrptr[1], desc.c_str(), MAXSTR);
    }
  }

  /**
  * @brief Reload the last opened file
  */
  bool reload_input_file()
  {
    if (!last_loaded_file.empty())
      return load_file_show_graph(last_loaded_file.c_str());
    else
      return false;
  }
  /**
  * @brief Handle chooser line deletion. In fact we trigger a reload here
  */
  uint32 on_delete(uint32 n)
  {
    reload_input_file();
    return n;
  }

  /**
  * @brief Handle line insertion event. In fact we load a file here
  */
  void on_insert()
  {
    const char *filename = askfile_c(0, "*.bbgroup", "Please select BBGROUP file to load");
    if (filename == NULL)
      return;

    load_file_show_graph(filename);
  }

  /**
  * @brief Callback that handles ENTER or double clicks on a chooser node
  */
  void on_enter(uint32 n)
  {
    if (  gsgv == NULL 
       || gsgv->gv == NULL 
       || !IS_SEL(n) || 
       n > ch_nodes.size())
    {
      return;
    }

    gschooser_line_t &chn = ch_nodes[n-1];

    // Get the selected node group or first node group in the super group
    pnodegroup_t ng;
    if (chn.type == chlt_ng)
      ng = chn.ng;
    else if (chn.type == chlt_sg)
      ng = chn.sg->get_first_ng();
    else
      ng = NULL;

    if (ng != NULL)
    {
      // Select the current node
      int nid = gsgv->get_ng_id(ng);
      if (nid != -1)
        jump_to_node(gsgv->gv, nid);
    }
  }

  /**
  * @brief Callback that handles node selection
  */
  void highlight_node(uint32 n)
  {
    if (   gsgv == NULL 
        || gsgv->gv == NULL)
    {
      return;
    }

    gschooser_line_t &chn = ch_nodes[n];

    // Clear previous highlight
    gsgv->clear_highlighting(true);

    DECL_CG;

    switch (chn.type)
    {
      //
      // Group management
      //
      case chlt_gm:
      {
        // Get all super groups
        psupergroup_listp_t sgroups = gm->get_path_sgl();

        // Mark them for selection
        gsgv->highlight_nodes(
            sgroups, 
            cg, 
            true);

        break;
      }
      //
      // Node groups and supergroups
      //
      case chlt_ng:
      case chlt_sg:
      {
        colorvargen_t cv;
        bgcolor_t clr;

        if (chn.type == chlt_ng)
        {
          // Pick a color
          cg.get_colorvar(cv);
          clr = cg.get_color_anyway(cv);

          gsgv->highlight_nodes(
              chn.ng, 
              clr, 
              true);
        }
        // super groups - chnt_sg
        else
        {
          // Use one color for all the different node group list
          gsgv->highlight_nodes(
            chn.ngl, 
            cg, 
            true);
        }
        break;
      }
      default:
        return;
    }
    if (!options.manual_refresh_mode)
      gsgv->refresh_view();
  }

  /**
  * @brief The chooser is closed
  */
  void on_destroy()
  {
    if (chi.popup_names != NULL)
      qfree((void *)chi.popup_names);

    // Delete the group manager
    delete gm;
    gm = NULL;

    // Close the associated graph
    close_graph();
    delete_singleton();
  }

  /**
  * @brief Refresh the chooser lines
  */
  void refresh()
  {
    refresh_chooser(STR_GS_PANEL);
  }

  /**
  * @brief Handles chooser refresh request
  */
  void on_refresh()
  {
  }

  /**
  * @brief Load and display a bbgroup file
  */
  bool load_file_show_graph(const char *filename)
  {
    // Retrieve the options
    options.load_options();

    // Show we show the options dialog again?
    if (options.show_options_dialog_next_time)
      options.show_dialog();

    // Load the input file
    if (!load_file(filename))
      return false;

    // Show the graph
    gsgv = gsgraphview_t::show_graph(
              &func_fc, 
              gm, 
              &options);
    if (gsgv == NULL)
      return false;

    gsgv->set_parentref(&gsgv);

    // Remember last loaded file
    last_loaded_file = filename;

    return true;
  }

  /**
  * @brief Populate chooser lines
  */
  void populate_chooser_lines()
  {
    // TODO: do not delete previous lines
	// TODO: add option to show similar_sgs
    ch_nodes.clear();

    // Add the first-level node = bbgroup file
    gschooser_line_t *line = &ch_nodes.push_back();
    line->type = chlt_gm;
    line->gm = gm;

    psupergroup_listp_t sgroups = gm->get_path_sgl();
    for (supergroup_listp_t::iterator it=sgroups->begin();
         it != sgroups->end();
         ++it)
    {
      supergroup_t &sg = **it;

      // Add the second-level node = a set of group defs
      line = &ch_nodes.push_back();
      nodegroup_list_t &ngl = sg.groups;
      line->type = chlt_sg;
      line->gm   = gm;
      line->sg   = &sg;
      line->ngl  = &ngl;

      // Add each nodedef list within each node group
      for (nodegroup_list_t::iterator it = ngl.begin();
           it != ngl.end();
           ++it)
      {
        pnodegroup_t ng = *it;
        // Add the third-level node = nodedef
        line = &ch_nodes.push_back();
        line->type = chlt_ng;
        line->gm   = gm;
        line->sg   = &sg;
        line->ngl  = &ngl;
        line->ng   = ng;
      }
    }
  }

  /**
  * @brief Handles chooser initialization
  */
  void on_init()
  {
#ifdef _DEBUG
    const char *fn;
    
    //TODO: fix me; file not found -> crash on exit
    //fn = "P:\\projects\\experiments\\bbgroup\\sample_c\\bin\\v1\\x86\\f1.bbgroup";

    //fn = "P:\\projects\\experiments\\bbgroup\\sample_c\\bin\\v1\\x86\\main.bbgroup";
    //fn = "P:\\Tools\\idadev\\plugins\\workfile-1.bbgroup";
    fn = "P:\\projects\\experiments\\bbgroup\\sample_c\\InlineTest\\f1.bbgroup";
    //fn = "P:\\projects\\experiments\\bbgroup\\sample_c\\InlineTest\\doit.bbgroup";
    load_file_show_graph(fn);
#endif
  }

  /**
  * @brief Initialize the chooser3 structure
  */
  void init_chi()
  {
    static const int widths[] = {60, 16};

    memset(&chi, 0, sizeof(chi));
    chi.cb = sizeof(chi);
    chi.flags = 0;
    chi.width = -1;
    chi.height = -1;
    chi.title = STR_GS_PANEL;
    chi.obj = this;
    chi.columns = qnumber(widths);
    chi.widths = widths;

    chi.icon  = -1;
    chi.deflt = -1;

    chi.sizer       = s_sizer;
    chi.getl        = s_getl;
    chi.ins         = s_ins;
    chi.del         = s_del;
    chi.enter       = s_enter;
    chi.destroyer   = s_destroyer;
    chi.refresh     = s_refresh;
    chi.select      = s_select;
    chi.edit        = s_edit;
    chi.initializer = s_initializer;


    chi.popup_names = (const char **)qalloc(sizeof(char *) * 5);
    *(((char **)chi.popup_names)+0) = "Load bbgroup file"; // Insert
    *(((char **)chi.popup_names)+1) = "Reload bbgroup file"; // Delete
    *(((char **)chi.popup_names)+2) = "Edit description"; // Edit
    *(((char **)chi.popup_names)+3) = NULL; // Refresh
    *(((char **)chi.popup_names)+4) = NULL; // Copy

    //static uint32 idaapi *s_update(void *obj, uint32 n);
    //static int idaapi s_get_icon)(void *obj, uint32 n);
    //void (idaapi *get_attrs)(void *obj, uint32 n, chooser_item_attrs_t *attrs);
  }

public:
  /**
  * @brief Constructor
  */
  gschooser_t()
  {
    init_chi();

    gsgv = NULL;
    gm = NULL;
  }

  /**
  * @brief Destructor
  */
  ~gschooser_t()
  {
    //NOTE: IDA will close the chooser for us and thus the destroy callback will be called
  }

  /**
  * @brief Close the graph view
  */
  void close_graph()
  {
    // Make sure the gv was not closed independently
    if (gsgv == NULL || gsgv->form == NULL)
      return;

    // Close the graph-view hosting form
    close_tform(gsgv->form, 0);
  }

  /**
  * @brief Load the file bbgroup file into the chooser
  */
  bool load_file(const char *filename)
  {
    // Delete the previous group manager
    delete gm;
    gm = new groupman_t();

    // Load a file and parse it
    // (don't init cache yet because file may be optimized)
    if (!gm->parse(filename, false))
    {
      msg(STR_GS_MSG "Error: failed to parse group file '%s'\n", filename);
      delete gm;
      return false;
    }

    // Get an address from the parsed file
    nodedef_t *nd = gm->get_first_nd();
    if (nd == NULL)
    {
      msg(STR_GS_MSG "Invalid input file! No addresses defined\n");
      return false;
    }

    // Get related function
    func_t *f = get_func(nd->start);
    if (f == NULL)
    {
      msg("Input file does not related to a defined function!\n");
      return false;
    }

    // Build the flowchart once
    if (!get_func_flowchart(f->startEA, func_fc))
    {
      msg("Could not build function flow chart at %a\n", f->startEA);
      return false;
    }

    // De-optimize the input file
    if (sanitize_groupman(BADADDR, gm, &func_fc))
    {
      // Now initialize the cache
      gm->initialize_lookups();
    }

    populate_chooser_lines();
    return true;
  }

  /**
  * @brief Show the chooser
  */
  static void show()
  {
    if (singleton == NULL)
      singleton = new gschooser_t();

    choose3(&singleton->chi);
    set_dock_pos(STR_GS_PANEL, STR_OUTWIN_TITLE, DP_RIGHT);
    set_dock_pos(STR_GS_VIEW, STR_IDAVIEWA_TITLE, DP_INSIDE);
  }
};
gschooser_t *gschooser_t::singleton = NULL;

//--------------------------------------------------------------------------
//
//      PLUGIN CALLBACKS
//
//--------------------------------------------------------------------------


//--------------------------------------------------------------------------
void idaapi run(int /*arg*/)
{
  gschooser_t::show();
}

//--------------------------------------------------------------------------
int idaapi init(void)
{
  return is_ida_gui() ? PLUGIN_OK : PLUGIN_SKIP;
}

//--------------------------------------------------------------------------
void idaapi term(void)
{
}

//--------------------------------------------------------------------------
//
//      PLUGIN DESCRIPTION BLOCK
//
//--------------------------------------------------------------------------
plugin_t PLUGIN =
{
  IDP_INTERFACE_VERSION,
  0,                    // plugin flags
  init,                 // initialize

  term,                 // terminate. this pointer may be NULL.

  run,                  // invoke plugin

  "",                   // long comment about the plugin
                        // it could appear in the status line
                        // or as a hint

  "",                   // multiline help about the plugin

  "GraphSlick",         // the preferred short name of the plugin
  "Ctrl-4"              // the preferred hotkey to run the plugin
};