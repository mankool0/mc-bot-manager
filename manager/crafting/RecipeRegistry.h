#ifndef RECIPEREGISTRY_H
#define RECIPEREGISTRY_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QVector>
#include <QJsonObject>

// Recipe structures
struct RecipeIngredient {
    int slot = 0;
    QStringList items;  // Possible items (for tags)
    int count = 1;
};

struct Recipe {
    QString recipeId;
    QString type;  // e.g., "minecraft:crafting_shaped"
    QString resultItem;
    int resultCount = 1;

    QVector<RecipeIngredient> ingredients;
    bool isShapeless = false;

    // Optional: for smelting/cooking recipes
    float experience = 0.0f;
    int cookingTime = 0;
};

class RecipeRegistry
{
public:
    RecipeRegistry();

    // Loading/saving
    bool loadFromJson(const QJsonObject &recipesJson, const QJsonObject &tagsJson);
    bool loadFromCache(const QString &version);  // Load from cache, download if needed
    QJsonObject recipesToJson() const;
    QJsonObject tagsToJson() const;

    // Accessors
    const Recipe* getRecipe(const QString &recipeId) const;
    QStringList getAllRecipeIds() const;
    int getRecipeCount() const;
    int getTagCount() const;

    // Tag operations
    QStringList expandTag(const QString &tagOrItem) const;
    bool hasTag(const QString &tagName) const;

    void clear();

    // Static cache/download helpers
    static QString getRecipeCachePath(const QString &version);
    static QString getTagCachePath(const QString &version);
    static bool downloadRecipes(const QString &version, const QString &cachePath);
    static bool downloadTags(const QString &version, const QString &cachePath);

private:
    QMap<QString, Recipe> recipes;
    QMap<QString, QStringList> tags;

    // Parsing helpers
    void parseRecipe(const QString &recipeId, const QJsonObject &recipeData);
    QStringList parseIngredient(const QJsonValue &ingredientValue) const;
    QStringList expandTagRecursive(const QString &tagOrItem, QSet<QString> &visited) const;

    // Type-specific parsers
    void parseCraftingShaped(Recipe &recipe, const QJsonObject &data);
    void parseCraftingShapeless(Recipe &recipe, const QJsonObject &data);
    void parseSmelting(Recipe &recipe, const QJsonObject &data);
    void parseSmithing(Recipe &recipe, const QJsonObject &data);
    void parseStonecutting(Recipe &recipe, const QJsonObject &data);
    void parseCraftingTransmute(Recipe &recipe, const QJsonObject &data);
};

#endif // RECIPEREGISTRY_H
