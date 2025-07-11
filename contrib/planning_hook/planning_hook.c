#include "postgres.h"
#include "fmgr.h"
#include "optimizer/planner.h"
#include "optimizer/pathnode.h"
#include "utils/elog.h"
#include "nodes/print.h"
#include "commands/explain_state.h"
#include "commands/explain_dr.h"

PG_MODULE_MAGIC;

void _PG_fini(void);

/* Global variable to hold the root planner info */
extern PlannerInfo *global_root;

/* Original planner hook pointer */
static planner_hook_type prev_planner_hook = NULL;

/* Print used for printing the selected plan */
static void print_plan_tree(Plan *plan, int indent)
{
    const char *type;
    char prefix[64];
    if (!plan) return;

    switch (nodeTag(plan)) {
        case T_SeqScan: type = "Seq Scan"; break;
        case T_IndexScan: type = "Index Scan"; break;
        case T_BitmapHeapScan: type = "Bitmap Heap Scan"; break;
        case T_NestLoop: type = "Nested Loop Join"; break;
        case T_HashJoin: type = "Hash Join"; break;
        case T_MergeJoin: type = "Merge Join"; break;
        case T_Agg: type = "Aggregate"; break;
        case T_Sort: type = "Sort"; break;
        case T_Hash: type = "Hash"; break;
        case T_Append: type = "Append"; break;
        default: type = "Other Plan"; break;
    }

    memset(prefix, ' ', sizeof(prefix));
    prefix[indent * 2] = 0;

    elog(INFO, "%sSelected Plan: %s  cost=%.2f..%.2f rows=%.0f width=%d",
         prefix, type,
         plan->startup_cost, plan->total_cost,
         plan->plan_rows, plan->plan_width);


    if (plan->lefttree)
        print_plan_tree(plan->lefttree, indent + 1);
    if (plan->righttree)
        print_plan_tree(plan->righttree, indent + 1);
}

static void print_path_info(Path *path, int level)
{
    const char *type;
    char indent[32];

    memset(indent, ' ', sizeof(indent));
    indent[sizeof(indent) - 1] = '\0';
    indent[level * 2] = '\0'; // two spaces per level

    switch (nodeTag(path))
    {
        case T_SeqScan:      type = "Seq Scan"; break;
        case T_IndexPath:        type = "Index Scan"; break;
        case T_BitmapHeapPath:   type = "Bitmap Heap Scan"; break;
        case T_NestPath:         type = "Nested Loop Join"; break;
        case T_HashPath:         type = "Hash Join"; break;
        case T_MergePath:        type = "Merge Join"; break;
        case T_AppendPath:       type = "Append"; break;

        default:                 type = "Other Path"; break;
    }
    elog(INFO, "%sPath: %s  cost=%.2f..%.2f  rows=%.0f", indent, type,
        path->startup_cost, path->total_cost, path->rows);

    /* If this is a join path, recursively print children */
    if (IsA(path, NestPath) || IsA(path, MergePath) || IsA(path, HashPath))
    {
        JoinPath *jpath = (JoinPath *) path;
        elog(INFO, "%s  Outer:", indent);
        print_path_info(jpath->outerjoinpath, level + 1);
        elog(INFO, "%s  Inner:", indent);
        print_path_info(jpath->innerjoinpath, level + 1);
    }
}

/* Custom planner hook function */
static PlannedStmt * my_planner_hook(Query *parse, const char *query_string,
                int cursorOptions, ParamListInfo boundParams)
{
    
    PlannedStmt *result = NULL;
    PlannerInfo* root;
    RelOptInfo *rel;
    ListCell *lc;
    ListCell *lc2;
    /* Call the original planner to build paths */
    if (prev_planner_hook)
        result = prev_planner_hook(parse, query_string, cursorOptions, boundParams);
    else
        result = standard_planner(parse, query_string, cursorOptions, boundParams);

    /* After planner runs, examine all relations and their paths */
   root= global_root;
    if(root == NULL)
    {
        elog(INFO, "Global root is NULL. Cannot log paths.");
        return result;
    }
    elog(INFO, "\nLogging paths for all relations in the query...\n");
    print_plan_tree(result->planTree, 0);
    

    for (int i = 1; i < root->simple_rel_array_size; i++){
        rel = root->simple_rel_array[i];
        if (rel)
        {
            elog(INFO, "Relation #%d", i);
            foreach(lc, rel->pathlist)
            {
                Path *path = (Path *) lfirst(lc);
                print_path_info(path, 1);
            }
        }
    }

    /* Print join rels (if any) */
    
    foreach(lc, root->join_rel_list)
    {
        rel = (RelOptInfo *) lfirst(lc);
        if (rel)
        {
            elog(INFO, "Join Relation %p", rel);
            
            foreach(lc2, rel->pathlist)
            {
                Path *path = (Path *) lfirst(lc2);
                print_path_info(path, 1);
            }
        }
    }

    return result;
}

/* Module load callback */
void
_PG_init(void)
{
    prev_planner_hook = planner_hook;
    planner_hook = my_planner_hook;
}

/* Module unload callback */
void
_PG_fini(void)
{
    planner_hook = prev_planner_hook;
}
