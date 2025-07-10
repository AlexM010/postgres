#include "postgres.h"
#include "fmgr.h"
#include "optimizer/planner.h"
#include "optimizer/pathnode.h"
#include "utils/elog.h"

PG_MODULE_MAGIC;
extern PlannerInfo *global_root;
/* Original planner hook pointer */
static planner_hook_type prev_planner_hook = NULL;

static void print_path(Path *path, int level)
{
    char prefix[32];
    memset(prefix, ' ', sizeof(prefix));
    prefix[level * 2] = 0;  // indentation

    const char *type;
    switch (nodeTag(path))
    {
        case T_NestPath:  type = "Nested Loop Join"; break;
        case T_HashPath:  type = "Hash Join"; break;
        case T_MergePath: type = "Merge Join"; break;
        case T_SeqScanState: type = "Seq Scan"; break;
        case T_IndexPath:  type = "Index Scan"; break;
        case T_BitmapHeapPath: type = "Bitmap Heap Scan"; break;
        case T_AppendPath: type = "Append"; break;
        case T_BitmapAndPath: type = "Bitmap And"; break;
        case T_BitmapOrPath: type = "Bitmap Or"; break;
        case T_TidPath:    type = "TID Scan"; break;
        case T_SubqueryScanPath: type = "Subquery Scan"; break;
        
        default:          type = "Other Path"; break;
    }

    elog(INFO, "%sPath type: %s | cost=%.2f | rows=%.2f", prefix, type, path->total_cost, path->rows);

    // If it's a join, print child paths
    if ((IsA(path, NestPath) || IsA(path, MergePath) || IsA(path, HashPath)))
    {
        JoinPath *jpath = (JoinPath *) path;
        elog(INFO, "%s  [Join left:]", prefix);
        print_path(jpath->outerjoinpath, level + 1);
        elog(INFO, "%s  [Join right:]", prefix);
        print_path(jpath->innerjoinpath, level + 1);
    }
}

static void log_paths(RelOptInfo *rel)
{
    ListCell *lc;

    if (!rel || !rel->pathlist)
        return;

    foreach(lc, rel->pathlist)
    {
        Path *path = (Path *) lfirst(lc);

        const char *type;
        print_path(path, 0);
        switch (nodeTag(path))
        {
            case T_Path:           type = "Seq Scan"; break;
            case T_IndexPath:      type = "Index Scan"; break;
            case T_BitmapHeapPath: type = "Bitmap Heap Scan"; break;
            case T_NestPath:       type = "Nested Loop Join"; break;
            case T_HashPath:       type = "Hash Join"; break;
            case T_MergePath:      type = "Merge Join"; break;
            case T_AppendPath:     type = "Append"; break;
            default:               type = "Other Path"; break;
        }

        elog(INFO, "Path type: %s | cost=%.2f | rows=%.2f", type, path->total_cost, path->rows);
    }
}


/* Our custom planner hook function */
static PlannedStmt *
my_planner_hook(Query *parse, const char *query_string,
                int cursorOptions, ParamListInfo boundParams)
{
    
    PlannedStmt *result = NULL;
    PlannerInfo* root;
    RelOptInfo *rel;
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
    for (int i = 1; i < root->simple_rel_array_size; i++)  // skip index 0 (reserved)
    {
        printf("Examining relation %d\n", i);
        rel = root->simple_rel_array[i];
        if (rel)
            log_paths(rel);
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
