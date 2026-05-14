#ifndef CRAFTINGPLANNER_H
#define CRAFTINGPLANNER_H

#include <QString>
#include <QMap>
#include <QSet>
#include <QList>
#include <QVector>

class RecipeRegistry;
class ItemRegistry;
struct RecipeIngredient;
struct Recipe;

// Represents a single crafting operation in the plan
struct CraftingStep {
    QString recipeId;                    // Which recipe to use
    int times;                           // How many times to execute
    QMap<QString, int> inputs;           // Actual items consumed (tags resolved)
    QString outputItem;                  // What this produces
    int outputCount;                     // Total output (times * recipe.resultCount)

    // If true this is not a crafting step but a consolidation request:
    // merge all partial stacks of `outputItem` to free inventory slots.
    // `times` and `inputs` are unused when isConsolidate is true.
    bool isConsolidate = false;
};

// Complete crafting plan with execution order and resource requirements
struct CraftingPlan {
    QList<CraftingStep> steps;           // In execution order (dependencies first)
    QMap<QString, int> rawMaterials;     // Items needed but not craftable
    QMap<QString, int> leftovers;        // Excess production
    bool success;
    QString error;
};

class CraftingPlanner
{
public:
    CraftingPlanner(RecipeRegistry* registry, ItemRegistry* itemRegistry = nullptr);

    // Main entry point: plan crafting with recursive algorithm.
    // inventory is COPIED (not modified).
    // initialStacks: per-slot item counts from the actual bot inventory (item -> list of stack
    //   sizes for each occupied slot). Used for space-aware scheduling. When non-empty, a
    //   scheduleForSpace pass is run after planning to split/reorder steps and insert
    //   consolidation steps so the plan never exceeds maxSlots inventory slots.
    CraftingPlan planCrafting(
        const QString& targetItem,
        int quantity,
        const QMap<QString, int>& inventory,
        const QSet<QString>& blacklistedItems = QSet<QString>(),
        bool recursive = true,
        const QMap<QString, QList<int>>& initialStacks = QMap<QString, QList<int>>(),
        int maxSlots = 36
    );

private:
    RecipeRegistry* m_registry;
    ItemRegistry* m_itemRegistry;

    // Internal state for the recursion
    struct PlanningState {
        QMap<QString, int> virtualInventory; // Current inventory state during planning
        QMap<QString, int> originalInventory; // Original starting inventory (never modified)
        QSet<QString> blacklistedItems;
        bool recursive;
        CraftingPlan plan;
        int depth; // Recursion depth safety
    };

    // Recursive function to ensure an item (or tag) is available in the virtual inventory
    bool satisfyDependency(
        const QStringList& possibleItems,
        int quantity,
        PlanningState& state,
        QMap<QString, int>* consumed = nullptr,
        bool isRootTarget = false
    );

    const Recipe* pickBestRecipe(
        const QStringList& possibleTargets,
        const PlanningState& state,
        QString* selectedItem = nullptr
    );

    int calculateRawMaterialCost(
        const Recipe* recipe,
        int batches,
        const PlanningState& state,
        int maxDepth = 10,
        QSet<QString>* inProgress = nullptr,
        QMap<QString, int>* memo = nullptr
    ) const;

    const Recipe* pickBestRecipeForItem(
        const QString& item,
        const PlanningState& state
    ) const;

    // Space-aware scheduling post-process:
    // Walks the plan steps in dependency order, simulates slot usage using
    // proper Minecraft stacking semantics, and splits steps / inserts
    // consolidation steps to keep slot count within maxSlots at all times.
    void scheduleForSpace(
        CraftingPlan& plan,
        const QMap<QString, QList<int>>& initialStacks,
        int maxSlots
    );

    // ---- Stack simulation helpers ----

    int getStackSize(const QString& item) const;

    // Total occupied inventory slots across all items
    static int totalSlotsInMap(const QMap<QString, QList<int>>& stacks);

    // Add count items to stacks using Minecraft fill-partial-first semantics
    static void addToStacks(QMap<QString, QList<int>>& stacks,
                            const QString& item, int count, int maxStack);

    // Remove count items from stacks, consuming smallest stacks first to free slots
    static void removeFromStacks(QMap<QString, QList<int>>& stacks,
                                 const QString& item, int count);

    // Merge all partial stacks of item into as few slots as possible
    void simulateConsolidation(QMap<QString, QList<int>>& stacks,
                               const QString& item) const;

    // Simulate executing a crafting step (outStep.inputs = total consumed, not per-exec)
    void simulateStepOnStacks(const CraftingStep& outStep,
                              QMap<QString, QList<int>>& stacks) const;

    // Max executions possible given current stacks (limited by available materials)
    // step.inputs must be per-execution amounts
    int maxByMaterials(const CraftingStep& step, int remaining,
                       const QMap<QString, QList<int>>& stacks) const;

    // Max executions that keep total slots within maxSlots after the step.
    // step.inputs must be per-execution amounts; resultCount = items produced per execution.
    int maxBySpace(const CraftingStep& step, int remaining, int resultCount,
                   const QMap<QString, QList<int>>& stacks, int maxSlots) const;

    // Among items currently in stacks that are also used as ingredients in pending steps,
    // return the one where consolidation would free the most slots (empty if none).
    struct PendingEntry { CraftingStep step; int remaining; int resultCount; };
    QString findConsolidationCandidate(const QMap<QString, QList<int>>& stacks,
                                       const QList<PendingEntry>& pending) const;
};

#endif // CRAFTINGPLANNER_H
