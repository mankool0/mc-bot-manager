#include "CraftingPlanner.h"
#include "RecipeRegistry.h"
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <chrono>

CraftingPlanner::CraftingPlanner(RecipeRegistry* registry)
    : m_registry(registry)
{
}

// Helper to sort inventory items by quantity (Ascending)
// This implements the "Free up space" heuristic by using small stacks first.
struct StackOption {
    QString item;
    int quantity;
    
    // Sort logic: Smallest stacks first
    bool operator<(const StackOption& other) const {
        return quantity < other.quantity;
    }
};

int CraftingPlanner::calculateRawMaterialCost(
    const Recipe* recipe,
    int batches,
    const PlanningState& state,
    int maxDepth) const
{
    if (!recipe || maxDepth <= 0) return 999999;

    qDebug() << QString(10 - maxDepth, ' ') << "[Cost] Calculating" << recipe->recipeId << "x" << batches << "depth=" << maxDepth;

    // Cost metric: number of crafting steps required
    // This naturally favors shorter recipe chains
    int totalCraftingSteps = batches; // This recipe itself costs 'batches' operations

    for (const auto& ing : recipe->ingredients) {
        int needed = ing.count * batches;

        // Check if we can get this from current inventory
        int availableInInventory = 0;
        for (const QString& item : ing.items) {
            availableInInventory += state.virtualInventory.value(item, 0);
        }

        int stillNeeded = qMax(0, needed - availableInInventory);

        if (stillNeeded > 0) {
            // Need to get this ingredient - find cheapest way (craft or gather)
            int minSubCost = 999999;

            qDebug() << QString(10 - maxDepth, ' ') << "  Ingredient:" << ing.items << "need=" << stillNeeded;

            for (const QString& item : ing.items) {
                const Recipe* subRecipe = m_registry->getRecipe(item);

                if (subRecipe) {
                    // This item can be crafted
                    int subBatches = (stillNeeded + subRecipe->resultCount - 1) / subRecipe->resultCount;
                    int subCost = calculateRawMaterialCost(subRecipe, subBatches, state, maxDepth - 1);
                    minSubCost = qMin(minSubCost, subCost);
                } else {
                    // This is a raw material (no recipe)
                    // We still need some, so we need to gather it - always high cost
                    minSubCost = qMin(minSubCost, 999999);
                }
            }

            totalCraftingSteps += minSubCost;
            qDebug() << QString(10 - maxDepth, ' ') << "  Added cost:" << minSubCost << "total now:" << totalCraftingSteps;
        }
        // If availableInInventory >= needed, no additional cost (we already have it)
    }

    qDebug() << QString(10 - maxDepth, ' ') << "[Cost] Result for" << recipe->recipeId << "=" << totalCraftingSteps;
    return totalCraftingSteps;
}

const Recipe* CraftingPlanner::pickBestRecipe(
    const QStringList& possibleTargets,
    const PlanningState& state,
    QString* selectedItem)
{
    // If specific item, just return its recipe
    if (possibleTargets.size() == 1) {
        if (selectedItem) {
            *selectedItem = possibleTargets.first();
        }
        return m_registry->getRecipe(possibleTargets.first());
    }

    qDebug() << "  [PickRecipe] Selecting best recipe for one of:" << possibleTargets;

    const Recipe* bestRecipe = nullptr;
    QString bestItem;
    int lowestRawMaterialCost = 999999;
    long long maxCraftableBatches = -1;

    for (const QString& target : possibleTargets) {
        const Recipe* r = m_registry->getRecipe(target);

        int rawMaterialCost;
        long long currentBatches;

        if (!r) {
            // No recipe - this is a raw material (e.g., logs from chopping, ores from mining)
            // Check if we have any in inventory
            int availableQty = state.virtualInventory.value(target, 0);

            if (availableQty > 0) {
                // We have this raw material - BEST cost (we already have it)
                rawMaterialCost = 0;
                currentBatches = availableQty;
                qDebug() << "    - " << target << " (raw material): batches=" << currentBatches << " raw_cost=" << rawMaterialCost << "(in inventory)";
            } else {
                // We don't have it and can't craft it - WORST cost (need to gather)
                rawMaterialCost = 999999;
                currentBatches = 0;
                qDebug() << "    - " << target << " (raw material): batches=" << currentBatches << " raw_cost=" << rawMaterialCost << "(need to gather)";
            }
        } else {
            // Has recipe - calculate crafting cost
            currentBatches = 999999999;

            for (const auto& ing : r->ingredients) {
                long long totalAvailable = 0;

                for (const QString& item : ing.items) {
                    int qty = state.virtualInventory.value(item, 0);
                    if (qty > 0) {
                        totalAvailable += qty;
                    }
                }

                if (ing.count > 0) {
                    long long batchesForThisIng = totalAvailable / ing.count;
                    currentBatches = std::min(currentBatches, batchesForThisIng);
                }
            }

            rawMaterialCost = calculateRawMaterialCost(r, 1, state);
            qDebug() << "    - " << target << " (" << r->recipeId << "): batches=" << currentBatches << " raw_cost=" << rawMaterialCost;
        }

        // Priority 1: Minimize raw material cost (most efficient recipe)
        // Priority 2: Prefer recipes using materials we HAD in original inventory
        // Priority 3: Maximize craftable batches (prefer recipes we can make now)

        bool updateBest = false;

        // First iteration - always set as best
        if (bestRecipe == nullptr && bestItem.isEmpty()) {
            qDebug() << "      [First] Setting" << target << "as initial best";
            updateBest = true;
        } else if (rawMaterialCost < lowestRawMaterialCost) {
            updateBest = true;
        } else if (rawMaterialCost == lowestRawMaterialCost) {
            // Same cost - use tiebreakers

            // Check if THIS recipe uses materials from original inventory
            bool thisHasOriginalMaterials = false;
            if (r) {
                for (const auto& ing : r->ingredients) {
                    for (const QString& item : ing.items) {
                        if (state.originalInventory.value(item, 0) > 0) {
                            thisHasOriginalMaterials = true;
                            break;
                        }
                    }
                    if (thisHasOriginalMaterials) break;
                }
            } else {
                // Raw material
                thisHasOriginalMaterials = state.originalInventory.value(target, 0) > 0;
            }

            // Check if BEST recipe uses materials from original inventory
            bool bestHasOriginalMaterials = false;
            if (bestRecipe) {
                for (const auto& ing : bestRecipe->ingredients) {
                    for (const QString& item : ing.items) {
                        if (state.originalInventory.value(item, 0) > 0) {
                            bestHasOriginalMaterials = true;
                            break;
                        }
                    }
                    if (bestHasOriginalMaterials) break;
                }
            } else if (!bestItem.isEmpty()) {
                // Best is a raw material
                bestHasOriginalMaterials = state.originalInventory.value(bestItem, 0) > 0;
            }

            qDebug() << "      [Tiebreak]" << target << "hasOriginal=" << thisHasOriginalMaterials << "vs best=" << bestItem << "hasOriginal=" << bestHasOriginalMaterials;

            // Prefer the one with original materials
            if (thisHasOriginalMaterials && !bestHasOriginalMaterials) {
                qDebug() << "        -> This has original materials, best doesn't - UPDATE";
                updateBest = true;
            } else if (thisHasOriginalMaterials == bestHasOriginalMaterials) {
                // Both have or both don't have - use batches as tiebreaker
                if (currentBatches > maxCraftableBatches) {
                    qDebug() << "        -> Same materials, more batches - UPDATE";
                    updateBest = true;
                }
            }
        }

        if (updateBest) {
            lowestRawMaterialCost = rawMaterialCost;
            maxCraftableBatches = currentBatches;
            bestRecipe = r;
            bestItem = target;
        }
    }

    if (selectedItem) {
        *selectedItem = bestItem;
    }

    if (bestRecipe) {
        qDebug() << "    => Selected:" << bestRecipe->resultItem << "(raw_cost:" << lowestRawMaterialCost << ", batches:" << maxCraftableBatches << ")";
    } else if (!bestItem.isEmpty()) {
        qDebug() << "    => Selected:" << bestItem << "(raw material, cost:" << lowestRawMaterialCost << ")";
    } else {
        qDebug() << "    => No valid option found";
    }

    return bestRecipe;
}

bool CraftingPlanner::satisfyDependency(
    const QStringList& possibleItems,
    int quantity,
    PlanningState& state,
    QMap<QString, int>* consumed,
    bool isRootTarget)
{
    QString indent = QString(state.depth * 2, ' ');

    if (state.depth > 100) {
        state.plan.error = "Max recursion depth reached - circular dependency?";
        qDebug() << indent << "ERROR: Max depth reached";
        return false;
    }
    state.depth++;

    int remainingNeeded = quantity;

    // 1. SCAVENGE: Consume from existing inventory
    // ---------------------------------------------------------
    // Skip scavenging if this is the root target - we want to PRODUCE, not just use existing items
    if (!isRootTarget) {
        QList<StackOption> options;
        for (const QString& item : possibleItems) {
            int qty = state.virtualInventory.value(item, 0);
            if (qty > 0) {
                options.append({item, qty});
            }
        }

        // Heuristic: Use smallest stacks first to clear slots
        std::sort(options.begin(), options.end());

        for (const auto& opt : options) {
            if (remainingNeeded <= 0) break;

            int take = qMin(opt.quantity, remainingNeeded);
            state.virtualInventory[opt.item] -= take;
            remainingNeeded -= take;

            // Track actual consumption
            if (consumed) {
                (*consumed)[opt.item] += take;
            }

            // Clean up map
            if (state.virtualInventory[opt.item] <= 0) {
                state.virtualInventory.remove(opt.item);
            }
        }

        if (remainingNeeded <= 0) {
            state.depth--;
            return true; // Satisfied purely from inventory
        }
    }

    // 2. CRAFT: Produce the deficit
    // ---------------------------------------------------------

    // We need to pick ONE item to craft to satisfy the remaining need.
    // Ideally one where we have materials.
    QString selectedItem;
    const Recipe* recipe = pickBestRecipe(possibleItems, state, &selectedItem);

    // Fail condition: No recipe found (or blacklisted/non-recursive)
    if (!recipe || (recipe && state.blacklistedItems.contains(recipe->resultItem)) || !state.recursive) {
        // Record as raw material needed
        QString itemToRequest = selectedItem.isEmpty() ? possibleItems.first() : selectedItem;
        state.plan.rawMaterials[itemToRequest] += remainingNeeded;
        
        // SIMULATION: Pretend we got it so planning can continue
        state.virtualInventory[itemToRequest] += remainingNeeded; // Add cheat items
        
        // Consume them immediately to balance the accounting
        state.virtualInventory[itemToRequest] -= remainingNeeded; 
        
        if (consumed) {
            (*consumed)[itemToRequest] += remainingNeeded;
        }
        
        state.depth--;
        return true;
    }
    
    // Calculate batches
    int producedPerBatch = recipe->resultCount;
    int batches = (remainingNeeded + producedPerBatch - 1) / producedPerBatch;
    int totalProduced = batches * producedPerBatch;

    // 3. RECURSE: Satisfy ingredients for the batches
    // ---------------------------------------------------------
    CraftingStep step;
    step.recipeId = recipe->recipeId;
    step.times = batches;
    step.outputItem = recipe->resultItem;
    step.outputCount = totalProduced;

    for (const auto& ing : recipe->ingredients) {
        int ingNeeded = ing.count * batches;
        QMap<QString, int> ingConsumed;
        
        // RECURSE with tracking
        if (!satisfyDependency(ing.items, ingNeeded, state, &ingConsumed)) {
            state.depth--;
            return false;
        }
        
        // Merge tracked consumption into step inputs
        for (auto it = ingConsumed.cbegin(); it != ingConsumed.cend(); ++it) {
            step.inputs[it.key()] += it.value();
        }
    }

    // 4. PRODUCE: Update state with results
    // ---------------------------------------------------------
    state.virtualInventory[recipe->resultItem] += totalProduced;
    state.plan.steps.append(step);

    // Finally, consume the produced items to satisfy THIS call's deficit
    state.virtualInventory[recipe->resultItem] -= remainingNeeded;
    
    if (consumed) {
        (*consumed)[recipe->resultItem] += remainingNeeded;
    }

    state.depth--;
    return true;
}

CraftingPlan CraftingPlanner::planCrafting(
    const QString& targetItem,
    int quantity,
    const QMap<QString, int>& inventory,
    const QSet<QString>& blacklistedItems,
    bool recursive)
{
    auto startTime = std::chrono::steady_clock::now();

    PlanningState state;
    state.virtualInventory = inventory;
    state.originalInventory = inventory; // Keep a copy of the starting inventory
    state.blacklistedItems = blacklistedItems;
    state.recursive = recursive;
    state.depth = 0;
    state.plan.success = false;

    QStringList target;
    target << targetItem;

    // Pass isRootTarget=true so we PRODUCE the item instead of scavenging from inventory
    if (satisfyDependency(target, quantity, state, nullptr, true)) {
        state.plan.success = true;

        // Calculate actual leftovers: items in final inventory that weren't in original inventory
        // This represents intermediate crafting byproducts that remain after all steps
        for (auto it = state.virtualInventory.cbegin(); it != state.virtualInventory.cend(); ++it) {
            const QString& item = it.key();
            int finalQty = it.value();
            int originalQty = state.originalInventory.value(item, 0);

            // Only count items we GAINED (not items we already had)
            if (finalQty > originalQty) {
                state.plan.leftovers[item] = finalQty - originalQty;
            }
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    qDebug() << "[CraftingPlanner] Planning completed in" << elapsed << "ms";

    return state.plan;
}