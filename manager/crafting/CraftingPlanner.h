#ifndef CRAFTINGPLANNER_H
#define CRAFTINGPLANNER_H

#include <QString>
#include <QMap>
#include <QSet>
#include <QList>
#include <QVector>

class RecipeRegistry;
struct RecipeIngredient;
struct Recipe;

// Represents a single crafting operation in the plan
struct CraftingStep {
    QString recipeId;                    // Which recipe to use
    int times;                           // How many times to execute
    QMap<QString, int> inputs;           // Actual items consumed (tags resolved)
    QString outputItem;                  // What this produces
    int outputCount;                     // Total output (times * recipe.resultCount)
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
    CraftingPlanner(RecipeRegistry* registry);

    // Main entry point: plan crafting with recursive algorithm
    // inventory is COPIED (not modified)
    CraftingPlan planCrafting(
        const QString& targetItem,
        int quantity,
        const QMap<QString, int>& inventory,
        const QSet<QString>& blacklistedItems = QSet<QString>(),
        bool recursive = true
    );

private:
    RecipeRegistry* m_registry;

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
    // Consumes the item from the virtual inventory before returning
    // If 'consumed' is provided, records exactly which items were taken to satisfy the request
    // If 'isRootTarget' is true, skips scavenging - we want to PRODUCE the item, not use existing inventory
    bool satisfyDependency(
        const QStringList& possibleItems,
        int quantity,
        PlanningState& state,
        QMap<QString, int>* consumed = nullptr,
        bool isRootTarget = false
    );

    // Pick the best recipe for a tag based on what materials we already have
    // Returns the recipe (or nullptr for raw materials) and sets selectedItem to the chosen item
    const Recipe* pickBestRecipe(
        const QStringList& possibleTargets,
        const PlanningState& state,
        QString* selectedItem = nullptr
    );

    // Calculate raw material cost for a recipe (recursive helper for pickBestRecipe)
    int calculateRawMaterialCost(
        const Recipe* recipe,
        int batches,
        const PlanningState& state,
        int maxDepth = 10
    ) const;
};

#endif // CRAFTINGPLANNER_H