#include "CraftingPlanner.h"
#include "RecipeRegistry.h"
#include "../world/ItemRegistry.h"
#include <QDebug>
#include <algorithm>
#include <chrono>
#include <limits>

CraftingPlanner::CraftingPlanner(RecipeRegistry* registry, ItemRegistry* itemRegistry)
    : m_registry(registry)
    , m_itemRegistry(itemRegistry)
{
}

// ---- Stack simulation helpers ----

int CraftingPlanner::getStackSize(const QString& item) const
{
    if (m_itemRegistry) {
        auto info = m_itemRegistry->getItem(item);
        if (info.has_value()) return info->maxStackSize;
    }
    return 64;
}

int CraftingPlanner::totalSlotsInMap(const QMap<QString, QList<int>>& stacks)
{
    int total = 0;
    for (const auto& s : stacks) total += s.size();
    return total;
}

void CraftingPlanner::addToStacks(QMap<QString, QList<int>>& stacks,
                                   const QString& item, int count, int maxStack)
{
    if (count <= 0) return;
    auto& s = stacks[item];
    // Fill existing partial stacks first
    for (int& c : s) {
        if (count <= 0) break;
        int space = maxStack - c;
        if (space > 0) {
            int add = qMin(space, count);
            c += add;
            count -= add;
        }
    }
    // Create new stacks for the remainder
    while (count > 0) {
        int add = qMin(count, maxStack);
        s.append(add);
        count -= add;
    }
}

void CraftingPlanner::removeFromStacks(QMap<QString, QList<int>>& stacks,
                                        const QString& item, int count)
{
    if (count <= 0 || !stacks.contains(item)) return;
    auto& s = stacks[item];
    // Consume smallest stacks first to free slots sooner
    std::sort(s.begin(), s.end());
    for (int& c : s) {
        if (count <= 0) break;
        int take = qMin(c, count);
        c -= take;
        count -= take;
    }
    s.erase(std::remove(s.begin(), s.end(), 0), s.end());
    if (s.isEmpty()) stacks.remove(item);
}

void CraftingPlanner::simulateConsolidation(QMap<QString, QList<int>>& stacks,
                                             const QString& item) const
{
    if (!stacks.contains(item)) return;
    auto& s = stacks[item];
    int total = 0;
    for (int c : s) total += c;
    int maxStack = getStackSize(item);
    s.clear();
    while (total > 0) {
        int add = qMin(total, maxStack);
        s.append(add);
        total -= add;
    }
}

void CraftingPlanner::simulateStepOnStacks(const CraftingStep& outStep,
                                            QMap<QString, QList<int>>& stacks) const
{
    // Consume ingredients (outStep.inputs = total amounts consumed)
    for (auto it = outStep.inputs.constBegin(); it != outStep.inputs.constEnd(); ++it) {
        removeFromStacks(stacks, it.key(), it.value());
    }
    // Produce output
    int outMaxStack = getStackSize(outStep.outputItem);
    addToStacks(stacks, outStep.outputItem, outStep.outputCount, outMaxStack);
}

int CraftingPlanner::maxByMaterials(const CraftingStep& step, int remaining,
                                     const QMap<QString, QList<int>>& stacks) const
{
    // Use step.inputs (aggregated per-execution amounts) rather than recipe->ingredients
    // to avoid double-counting when multiple ingredient slots consume the same item
    // (e.g. 8 plank slots in the chest recipe each check the same plank pool).
    int limit = remaining;
    for (auto it = step.inputs.constBegin(); it != step.inputs.constEnd(); ++it) {
        if (it.value() <= 0) continue;
        int available = 0;
        if (stacks.contains(it.key())) {
            for (int c : stacks[it.key()]) available += c;
        }
        limit = qMin(limit, available / it.value());
    }
    return limit;
}

int CraftingPlanner::maxBySpace(const CraftingStep& step, int remaining, int resultCount,
                                 const QMap<QString, QList<int>>& stacks, int maxSlots) const
{
    int outMaxStack = getStackSize(step.outputItem);

    // Fast path: check if the full amount fits without copying stacks
    {
        auto tempStacks = stacks;
        // Consume all inputs for `remaining` executions
        for (auto it = step.inputs.constBegin(); it != step.inputs.constEnd(); ++it) {
            removeFromStacks(tempStacks, it.key(), it.value() * remaining);
        }
        addToStacks(tempStacks, step.outputItem, remaining * resultCount, outMaxStack);
        if (totalSlotsInMap(tempStacks) <= maxSlots) return remaining;
    }

    // Binary search for the largest k that fits, then scan upward a few steps to handle
    // the rare non-monotone bumps at stack-fill boundaries.
    int lo = 1, hi = remaining - 1, best = 0;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        auto tempStacks = stacks;
        for (auto it = step.inputs.constBegin(); it != step.inputs.constEnd(); ++it)
            removeFromStacks(tempStacks, it.key(), it.value() * mid);
        addToStacks(tempStacks, step.outputItem, mid * resultCount, outMaxStack);
        if (totalSlotsInMap(tempStacks) <= maxSlots) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    for (int k = best + 1; k < remaining; k++) {
        auto tempStacks = stacks;
        for (auto it = step.inputs.constBegin(); it != step.inputs.constEnd(); ++it)
            removeFromStacks(tempStacks, it.key(), it.value() * k);
        addToStacks(tempStacks, step.outputItem, k * resultCount, outMaxStack);
        if (totalSlotsInMap(tempStacks) <= maxSlots) best = k; else break;
    }
    return best;
}

QString CraftingPlanner::findConsolidationCandidate(
    const QMap<QString, QList<int>>& stacks,
    const QList<PendingEntry>& pending) const
{
    // Collect all ingredient items referenced by pending steps
    QSet<QString> pendingIngredients;
    for (const auto& pe : pending) {
        const Recipe* recipe = m_registry->getRecipe(pe.step.recipeId);
        if (!recipe) continue;
        for (const auto& ing : recipe->ingredients) {
            for (const QString& itemId : ing.items) {
                pendingIngredients.insert(itemId);
            }
        }
    }

    QString best;
    int bestGain = 0;

    for (auto it = stacks.constBegin(); it != stacks.constEnd(); ++it) {
        const QString& item = it.key();
        const QList<int>& s = it.value();
        if (s.size() <= 1) continue;
        if (!pendingIngredients.contains(item)) continue;

        int total = 0;
        for (int c : s) total += c;
        int maxStack = getStackSize(item);
        int optimal = (total + maxStack - 1) / maxStack;
        int gain = s.size() - optimal;
        if (gain > bestGain) {
            bestGain = gain;
            best = item;
        }
    }

    return best;
}

void CraftingPlanner::scheduleForSpace(
    CraftingPlan& plan,
    const QMap<QString, QList<int>>& initialStacks,
    int maxSlots)
{
    if (plan.steps.isEmpty()) return;

    QMap<QString, QList<int>> vStacks = initialStacks;

    // Merge all steps for the same output item into one entry, preserving first-seen order.
    // The DFS emits one step per ingredient slot, so a recipe consuming N slots of the same
    // item produces N separate steps that should be executed as a single batch.
    QMap<QString, int> mergedIdx;
    QList<CraftingStep> mergedSteps;
    for (const auto& s : plan.steps) {
        if (s.times <= 0) continue;
        QString mergeKey = s.outputItem + ':' + s.recipeId;
        if (mergedIdx.contains(mergeKey)) {
            CraftingStep& existing = mergedSteps[mergedIdx[mergeKey]];
            existing.times += s.times;
            existing.outputCount += s.outputCount;
            for (auto it = s.inputs.constBegin(); it != s.inputs.constEnd(); ++it) {
                existing.inputs[it.key()] += it.value();
            }
        } else {
            mergedIdx[mergeKey] = mergedSteps.size();
            mergedSteps.append(s);
        }
    }

    // Build pending queue with per-execution inputs so we can scale correctly when splitting
    QList<PendingEntry> pending;
    for (const auto& s : mergedSteps) {
        const Recipe* recipe = m_registry->getRecipe(s.recipeId);
        int resultCount = recipe ? recipe->resultCount : 1;

        // Normalize inputs to per-execution amounts
        CraftingStep perExecStep = s;
        for (auto it = perExecStep.inputs.begin(); it != perExecStep.inputs.end(); ++it) {
            it.value() /= s.times;
        }
        pending.append({perExecStep, s.times, resultCount});
    }

    QList<CraftingStep> result;
    int stall = 0;

    while (!pending.isEmpty()) {
        PendingEntry& pe = pending.first();

        int byMats = maxByMaterials(pe.step, pe.remaining, vStacks);
        int bySpace = maxBySpace(pe.step, pe.remaining, pe.resultCount, vStacks, maxSlots);
        int toDo = qMin(byMats, bySpace);

        if (toDo == 0) {
            if (byMats > 0) {
                // Space is the bottleneck - see if consolidating a fragmented intermediate helps
                QString candidate = findConsolidationCandidate(vStacks, pending);
                if (!candidate.isEmpty()) {
                    CraftingStep consStep;
                    consStep.isConsolidate = true;
                    consStep.outputItem = candidate;
                    consStep.times = 0;
                    result.append(consStep);
                    simulateConsolidation(vStacks, candidate);
                    stall = 0;
                    continue;
                }
            } else if (bySpace > 0) {
                // byMats==0 with space available. Two causes:
                //   (a) A pending step needs to run first to produce a missing input.
                //   (b) An external raw material isn't reflected in vStacks yet
                //       (e.g. inventory update hasn't arrived at planning time).
                // Only stall for (a). For (b), proceed using bySpace as the limit -
                // the raw material will be physically present at execution time.
                bool blockedByPendingOutput = false;
                for (auto it = pe.step.inputs.constBegin(); it != pe.step.inputs.constEnd(); ++it) {
                    if (it.value() <= 0) continue;
                    int avail = 0;
                    if (vStacks.contains(it.key()))
                        for (int c : vStacks[it.key()]) avail += c;
                    if (avail < it.value()) {
                        // Input is short - check if any pending step produces it
                        for (int pi = 1; pi < pending.size(); pi++) {
                            if (pending[pi].step.outputItem == it.key()) {
                                blockedByPendingOutput = true;
                                break;
                            }
                        }
                    }
                    if (blockedByPendingOutput) break;
                }
                if (!blockedByPendingOutput) {
                    toDo = bySpace;
                }
            }

            if (toDo == 0) {
                // Can't make progress on this step - try later steps first
                pending.append(pending.takeFirst());
                stall++;
                if (stall >= pending.size()) {
                    // No combination of reordering can help; append remaining as-is
                    // (will fail at runtime if truly impossible, giving a clear error)
                    for (const auto& pe2 : std::as_const(pending)) {
                        CraftingStep outStep = pe2.step;
                        outStep.times = pe2.remaining;
                        outStep.outputCount = pe2.remaining * pe2.resultCount;
                        for (auto it = outStep.inputs.begin(); it != outStep.inputs.end(); ++it) {
                            it.value() *= pe2.remaining;
                        }
                        result.append(outStep);
                    }
                    break;
                }
                continue;
            }
        }

        stall = 0;

        // Build the emitted step with totals for toDo executions
        CraftingStep outStep = pe.step;
        outStep.times = toDo;
        outStep.outputCount = toDo * pe.resultCount;
        for (auto it = outStep.inputs.begin(); it != outStep.inputs.end(); ++it) {
            it.value() *= toDo;
        }
        result.append(outStep);

        simulateStepOnStacks(outStep, vStacks);

        if (toDo >= pe.remaining) {
            pending.removeFirst();
        } else {
            pe.remaining -= toDo;
        }
    }

    plan.steps = result;
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
    int maxDepth,
    QSet<QString>* inProgress,
    QMap<QString, int>* memo) const
{
    if (!recipe || maxDepth <= 0) return 999999;

    // Cycle detection: if this recipe is already on the current call stack,
    // we've hit a loop (e.g. gold_ingot -> gold_nugget -> gold_ingot).
    // Return worst-case cost to break the cycle.
    if (inProgress && inProgress->contains(recipe->recipeId)) return 999999;

    if (memo) {
        QString memoKey = recipe->recipeId + ':' + QString::number(batches);
        auto it = memo->constFind(memoKey);
        if (it != memo->constEnd()) return it.value();
    }

    if (inProgress) inProgress->insert(recipe->recipeId);

    qDebug() << QString(10 - maxDepth, ' ') << "[Cost] Calculating" << recipe->recipeId << "x" << batches << "depth=" << maxDepth;

    // Cost metric: number of crafting steps required
    // This naturally favors shorter recipe chains
    int totalCraftingSteps = batches; // This recipe itself costs 'batches' operations
    bool cycleTainted = false;

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
                QVector<const Recipe*> subRecipes = m_registry->getRecipesByResult(item);

                if (!subRecipes.isEmpty()) {
                    // This item can be crafted - find cheapest sub-recipe
                    for (const Recipe* subRecipe : std::as_const(subRecipes)) {
                        int subBatches = (stillNeeded + subRecipe->resultCount - 1) / subRecipe->resultCount;
                        if (inProgress && inProgress->contains(subRecipe->recipeId)) cycleTainted = true;
                        int subCost = calculateRawMaterialCost(subRecipe, subBatches, state, maxDepth - 1, inProgress, memo);
                        minSubCost = qMin(minSubCost, subCost);
                    }
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
    if (inProgress) inProgress->remove(recipe->recipeId);
    if (memo && !cycleTainted)
        (*memo)[recipe->recipeId + ':' + QString::number(batches)] = totalCraftingSteps;
    return totalCraftingSteps;
}

const Recipe* CraftingPlanner::pickBestRecipeForItem(
    const QString& item,
    const PlanningState& state) const
{
    QVector<const Recipe*> recipes = m_registry->getRecipesByResult(item);
    if (recipes.isEmpty()) return nullptr;
    if (recipes.size() == 1) return recipes.first();

    const Recipe* best = nullptr;
    int bestCost = 999999;
    long long bestBatches = -1;

    QMap<QString, int> memo;
    for (const Recipe* r : std::as_const(recipes)) {
        // Count craftable batches from current inventory
        long long batches = std::numeric_limits<long long>::max();
        for (const auto& ing : r->ingredients) {
            long long avail = 0;
            for (const QString& ingItem : ing.items) {
                avail += state.virtualInventory.value(ingItem, 0);
            }
            if (ing.count > 0) {
                batches = std::min(batches, avail / ing.count);
            }
        }

        QSet<QString> inProgress;
        int cost = calculateRawMaterialCost(r, 1, state, 10, &inProgress, &memo);
        if (best == nullptr || cost < bestCost ||
            (cost == bestCost && batches > bestBatches)) {
            best = r;
            bestCost = cost;
            bestBatches = batches;
        }
    }
    return best;
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
        return pickBestRecipeForItem(possibleTargets.first(), state);
    }

    qDebug() << "  [PickRecipe] Selecting best recipe for one of:" << possibleTargets;

    const Recipe* bestRecipe = nullptr;
    QString bestItem;
    int lowestRawMaterialCost = 999999;
    long long maxCraftableBatches = -1;
    QMap<QString, int> memo;

    for (const QString& target : possibleTargets) {
        const Recipe* r = pickBestRecipeForItem(target, state);

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

            QSet<QString> inProgress;
            rawMaterialCost = calculateRawMaterialCost(r, 1, state, 10, &inProgress, &memo);
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

    // Snapshot state before recursing so we can roll back if a circular
    // dependency is detected (i.e. the item we're crafting ends up being
    // needed as a raw material to produce one of its own ingredients).
    int stepsBefore = state.plan.steps.size();
    QMap<QString, int> inventoryBefore = state.virtualInventory;
    int rawMatBefore = state.plan.rawMaterials.value(recipe->resultItem, 0);

    // Blacklist the result item while satisfying its ingredients so that any
    // back-reference is caught by the raw-material fallback instead of
    // recursing until the depth limit is hit.
    bool addedToBlacklist = !state.blacklistedItems.contains(recipe->resultItem);
    if (addedToBlacklist) {
        state.blacklistedItems.insert(recipe->resultItem);
    }

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
            if (addedToBlacklist) state.blacklistedItems.remove(recipe->resultItem);
            state.depth--;
            return false;
        }

        // Merge tracked consumption into step inputs
        for (auto it = ingConsumed.cbegin(); it != ingConsumed.cend(); ++it) {
            step.inputs[it.key()] += it.value();
        }
    }

    if (addedToBlacklist) {
        state.blacklistedItems.remove(recipe->resultItem);
    }

    // Cycle detection: if our own item ended up in rawMaterials during the
    // ingredient recursion, the recipe is circular (e.g. redstone ->
    // redstone_block -> redstone).  Roll everything back and treat this item
    // as a plain raw material with just the amount originally needed.
    if (state.plan.rawMaterials.value(recipe->resultItem, 0) > rawMatBefore) {
        // Restore plan steps and virtual inventory to pre-recursion state
        while (state.plan.steps.size() > stepsBefore) {
            state.plan.steps.removeLast();
        }
        state.virtualInventory = inventoryBefore;
        state.plan.rawMaterials.remove(recipe->resultItem);
        if (rawMatBefore > 0) {
            state.plan.rawMaterials[recipe->resultItem] = rawMatBefore;
        }

        // Now record just what we actually need as a raw material
        QString itemToRequest = selectedItem.isEmpty() ? possibleItems.first() : selectedItem;
        state.plan.rawMaterials[itemToRequest] += remainingNeeded;
        state.virtualInventory[itemToRequest] += remainingNeeded;
        state.virtualInventory[itemToRequest] -= remainingNeeded;
        if (consumed) {
            (*consumed)[itemToRequest] += remainingNeeded;
        }

        state.depth--;
        return true;
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
    bool recursive,
    const QMap<QString, QList<int>>& initialStacks,
    int maxSlots)
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

    // Space-aware scheduling: split/reorder steps and insert consolidation hints
    // so the plan never exceeds maxSlots inventory slots.
    if (state.plan.success && !initialStacks.isEmpty()) {
        scheduleForSpace(state.plan, initialStacks, maxSlots);
    }

    auto endTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    qDebug() << "[CraftingPlanner] Planning completed in" << elapsed << "ms";

    return state.plan;
}