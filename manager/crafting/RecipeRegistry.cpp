#include "RecipeRegistry.h"
#include <QJsonArray>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>

RecipeRegistry::RecipeRegistry()
{
}

void RecipeRegistry::clear()
{
    recipes.clear();
    tags.clear();
}

bool RecipeRegistry::loadFromJson(const QJsonObject &recipesJson, const QJsonObject &tagsJson)
{
    clear();

    // Load tags first (needed for recipe parsing)
    for (auto it = tagsJson.constBegin(); it != tagsJson.constEnd(); ++it) {
        QString tagName = it.key();
        QJsonObject tagObj = it.value().toObject();

        QStringList items;
        if (tagObj.contains("values")) {
            QJsonArray itemsArray = tagObj["values"].toArray();
            for (const QJsonValue &val : itemsArray) {
                items.append(val.toString());
            }
        }

        if (!items.isEmpty()) {
            tags[tagName] = items;
        }
    }

    // Parse recipes
    for (auto it = recipesJson.constBegin(); it != recipesJson.constEnd(); ++it) {
        parseRecipe(it.key(), it.value().toObject());
    }

    return true;
}

bool RecipeRegistry::loadFromCache(const QString &version)
{
    QString recipeCachePath = getRecipeCachePath(version);
    QString tagCachePath = getTagCachePath(version);

    // Download if not cached
    if (!QFile::exists(recipeCachePath)) {
        if (!downloadRecipes(version, recipeCachePath)) {
            qWarning() << "Failed to download recipes for version" << version;
            return false;
        }
    }

    if (!QFile::exists(tagCachePath)) {
        if (!downloadTags(version, tagCachePath)) {
            qWarning() << "Failed to download tags for version" << version;
            return false;
        }
    }

    // Load recipes
    QFile recipeFile(recipeCachePath);
    if (!recipeFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open recipe cache:" << recipeCachePath;
        return false;
    }

    QByteArray recipeData = recipeFile.readAll();
    recipeFile.close();

    QJsonDocument recipeDoc = QJsonDocument::fromJson(recipeData);
    if (recipeDoc.isNull() || !recipeDoc.isObject()) {
        qWarning() << "Invalid recipe cache format for version" << version;
        return false;
    }

    // Load tags
    QFile tagFile(tagCachePath);
    if (!tagFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open tag cache:" << tagCachePath;
        return false;
    }

    QByteArray tagData = tagFile.readAll();
    tagFile.close();

    QJsonDocument tagDoc = QJsonDocument::fromJson(tagData);
    if (tagDoc.isNull() || !tagDoc.isObject()) {
        qWarning() << "Invalid tag cache format for version" << version;
        return false;
    }

    // Load into registry
    if (!loadFromJson(recipeDoc.object(), tagDoc.object())) {
        qWarning() << "Failed to parse recipes and tags for version" << version;
        return false;
    }

    qDebug() << "Loaded" << getRecipeCount() << "recipes and" << getTagCount() << "tags for version" << version;
    return true;
}

QJsonObject RecipeRegistry::recipesToJson() const
{
    QJsonObject root;

    for (auto it = recipes.constBegin(); it != recipes.constEnd(); ++it) {
        const Recipe &recipe = it.value();
        QJsonObject recipeObj;

        recipeObj["recipe_id"] = recipe.recipeId;
        recipeObj["type"] = recipe.type;
        recipeObj["result_item"] = recipe.resultItem;
        recipeObj["result_count"] = recipe.resultCount;
        recipeObj["is_shapeless"] = recipe.isShapeless;

        if (recipe.experience > 0) {
            recipeObj["experience"] = recipe.experience;
        }
        if (recipe.cookingTime > 0) {
            recipeObj["cooking_time"] = recipe.cookingTime;
        }

        QJsonArray ingredientsArray;
        for (const RecipeIngredient &ing : recipe.ingredients) {
            QJsonObject ingObj;
            ingObj["slot"] = ing.slot;
            ingObj["count"] = ing.count;

            QJsonArray itemsArray;
            for (const QString &item : ing.items) {
                itemsArray.append(item);
            }
            ingObj["items"] = itemsArray;

            ingredientsArray.append(ingObj);
        }
        recipeObj["ingredients"] = ingredientsArray;

        root[it.key()] = recipeObj;
    }

    return root;
}

QJsonObject RecipeRegistry::tagsToJson() const
{
    QJsonObject root;

    for (auto it = tags.constBegin(); it != tags.constEnd(); ++it) {
        QJsonArray itemsArray;
        for (const QString &item : it.value()) {
            itemsArray.append(item);
        }
        root[it.key()] = itemsArray;
    }

    return root;
}

const Recipe* RecipeRegistry::getRecipe(const QString &recipeId) const
{
    auto it = recipes.find(recipeId);
    if (it != recipes.end()) {
        return &it.value();
    }
    return nullptr;
}

QStringList RecipeRegistry::getAllRecipeIds() const
{
    return recipes.keys();
}

int RecipeRegistry::getRecipeCount() const
{
    return recipes.size();
}

int RecipeRegistry::getTagCount() const
{
    return tags.size();
}

QStringList RecipeRegistry::expandTag(const QString &tagOrItem) const
{
    QSet<QString> visited;
    return expandTagRecursive(tagOrItem, visited);
}

bool RecipeRegistry::hasTag(const QString &tagName) const
{
    QString name = tagName.startsWith("#") ? tagName.mid(1) : tagName;
    return tags.contains(name);
}

void RecipeRegistry::parseRecipe(const QString &recipeId, const QJsonObject &recipeData)
{
    Recipe recipe;
    recipe.recipeId = recipeId;
    recipe.type = recipeData["type"].toString();

    // Parse result (handle both string and object formats)
    QJsonValue resultValue = recipeData["result"];
    if (resultValue.isString()) {
        recipe.resultItem = resultValue.toString();
        recipe.resultCount = 1;
    } else if (resultValue.isObject()) {
        QJsonObject resultObj = resultValue.toObject();
        // Handle both "item" and "id" fields
        recipe.resultItem = resultObj.contains("item") ? resultObj["item"].toString() : resultObj["id"].toString();
        recipe.resultCount = resultObj.contains("count") ? resultObj["count"].toInt() : 1;
    }

    // Parse ingredients based on recipe type
    if (recipe.type.contains("crafting_shaped")) {
        parseCraftingShaped(recipe, recipeData);
    } else if (recipe.type.contains("crafting_shapeless")) {
        parseCraftingShapeless(recipe, recipeData);
    } else if (recipe.type.contains("smelting") || recipe.type.contains("blasting") ||
               recipe.type.contains("smoking") || recipe.type.contains("campfire_cooking")) {
        parseSmelting(recipe, recipeData);
    } else if (recipe.type.contains("smithing")) {
        parseSmithing(recipe, recipeData);
    } else if (recipe.type.contains("stonecutting")) {
        parseStonecutting(recipe, recipeData);
    } else if (recipe.type.contains("crafting_transmute")) {
        parseCraftingTransmute(recipe, recipeData);
    }
    // Special recipes (armor_dye, firework, etc.) don't have parseable ingredients
    // They're handled dynamically by Minecraft

    recipes[recipeId] = recipe;
}

QStringList RecipeRegistry::parseIngredient(const QJsonValue &ingredientValue) const
{
    if (ingredientValue.isString()) {
        QString str = ingredientValue.toString();
        return expandTag(str);
    } else if (ingredientValue.isObject()) {
        QJsonObject obj = ingredientValue.toObject();
        if (obj.contains("item")) {
            return QStringList{obj["item"].toString()};
        } else if (obj.contains("tag")) {
            QString tag = "#" + obj["tag"].toString();
            return expandTag(tag);
        }
    } else if (ingredientValue.isArray()) {
        // Array of possible ingredients - expand all
        QStringList result;
        QJsonArray arr = ingredientValue.toArray();
        for (const QJsonValue &val : arr) {
            result.append(parseIngredient(val));
        }
        return result;
    }
    return QStringList();
}

QStringList RecipeRegistry::expandTagRecursive(const QString &tagOrItem, QSet<QString> &visited) const
{
    // Detect circular references
    if (visited.contains(tagOrItem)) {
        qWarning() << "Circular tag reference detected:" << tagOrItem;
        return QStringList();
    }
    visited.insert(tagOrItem);

    // If it starts with '#', it's a tag reference
    if (tagOrItem.startsWith("#")) {
        QString tagName = tagOrItem.mid(1);  // Remove '#' prefix

        if (!tags.contains(tagName)) {
            // Not a tag we know about - might be a mod tag or future version
            // Just return it as-is for now
            return QStringList{tagOrItem};
        }

        QStringList result;
        for (const QString &value : tags[tagName]) {
            // Recursively expand (in case this tag contains other tags)
            result.append(expandTagRecursive(value, visited));
        }
        return result;
    } else {
        // It's a direct item ID
        return QStringList{tagOrItem};
    }
}

void RecipeRegistry::parseCraftingShaped(Recipe &recipe, const QJsonObject &data)
{
    recipe.isShapeless = false;

    QJsonArray pattern = data["pattern"].toArray();
    QJsonObject key = data["key"].toObject();

    for (int row = 0; row < pattern.size(); row++) {
        QString rowPattern = pattern[row].toString();
        for (int col = 0; col < rowPattern.length(); col++) {
            QChar ch = rowPattern[col];
            if (ch != ' ') {  // Not an empty slot
                RecipeIngredient ing;
                // Container slot 0 is result, slots 1-9 are the 3x3 crafting grid
                // Grid layout: 1,2,3 (top row), 4,5,6 (middle), 7,8,9 (bottom)
                ing.slot = (row * 3 + col) + 1;
                ing.count = 1;

                QJsonValue ingredientValue = key[QString(ch)];
                ing.items = parseIngredient(ingredientValue);

                if (!ing.items.isEmpty()) {
                    recipe.ingredients.append(ing);
                }
            }
        }
    }
}

void RecipeRegistry::parseCraftingShapeless(Recipe &recipe, const QJsonObject &data)
{
    recipe.isShapeless = true;

    QJsonArray ingredients = data["ingredients"].toArray();

    for (int i = 0; i < ingredients.size(); i++) {
        RecipeIngredient ing;
        // Shapeless recipes: slot 0 is result, slots 1-9 are grid
        // Use arbitrary grid positions starting at slot 1
        ing.slot = i + 1;
        ing.count = 1;

        ing.items = parseIngredient(ingredients[i]);

        if (!ing.items.isEmpty()) {
            recipe.ingredients.append(ing);
        }
    }
}

void RecipeRegistry::parseSmelting(Recipe &recipe, const QJsonObject &data)
{
    recipe.isShapeless = true;  // Position doesn't matter

    RecipeIngredient ing;
    ing.slot = 0;  // Input slot
    ing.count = 1;

    ing.items = parseIngredient(data["ingredient"]);

    if (!ing.items.isEmpty()) {
        recipe.ingredients.append(ing);
    }

    // Parse smelting-specific fields
    if (data.contains("experience")) {
        recipe.experience = data["experience"].toDouble();
    }
    if (data.contains("cookingtime")) {
        recipe.cookingTime = data["cookingtime"].toInt();
    }
}

void RecipeRegistry::parseSmithing(Recipe &recipe, const QJsonObject &data)
{
    recipe.isShapeless = true;

    // Smithing table slots: 0=template, 1=base, 2=addition, 3=result
    if (data.contains("template")) {
        RecipeIngredient templateIng;
        templateIng.slot = 0;
        templateIng.count = 1;
        templateIng.items = parseIngredient(data["template"]);
        if (!templateIng.items.isEmpty()) {
            recipe.ingredients.append(templateIng);
        }
    }

    if (data.contains("base")) {
        RecipeIngredient baseIng;
        baseIng.slot = 1;
        baseIng.count = 1;
        baseIng.items = parseIngredient(data["base"]);
        if (!baseIng.items.isEmpty()) {
            recipe.ingredients.append(baseIng);
        }
    }

    if (data.contains("addition")) {
        RecipeIngredient addIng;
        addIng.slot = 2;
        addIng.count = 1;
        addIng.items = parseIngredient(data["addition"]);
        if (!addIng.items.isEmpty()) {
            recipe.ingredients.append(addIng);
        }
    }
}

void RecipeRegistry::parseStonecutting(Recipe &recipe, const QJsonObject &data)
{
    recipe.isShapeless = true;

    RecipeIngredient ing;
    ing.slot = 0;  // Input slot
    ing.count = 1;

    ing.items = parseIngredient(data["ingredient"]);

    if (!ing.items.isEmpty()) {
        recipe.ingredients.append(ing);
    }

    // Stonecutting can have a count field for result
    if (data.contains("count")) {
        recipe.resultCount = data["count"].toInt();
    }
}

void RecipeRegistry::parseCraftingTransmute(Recipe &recipe, const QJsonObject &data)
{
    recipe.isShapeless = true;

    // Transmute recipes (e.g. dyeing shulker boxes): input + material in crafting grid
    if (data.contains("input")) {
        RecipeIngredient inputIng;
        inputIng.slot = 1;
        inputIng.count = 1;
        inputIng.items = parseIngredient(data["input"]);
        if (!inputIng.items.isEmpty()) {
            recipe.ingredients.append(inputIng);
        }
    }

    if (data.contains("material")) {
        RecipeIngredient matIng;
        matIng.slot = 2;
        matIng.count = 1;
        matIng.items = parseIngredient(data["material"]);
        if (!matIng.items.isEmpty()) {
            recipe.ingredients.append(matIng);
        }
    }
}

// Static cache/download helpers

QString RecipeRegistry::getRecipeCachePath(const QString &version)
{
    QString versionFormatted = version;
    versionFormatted.replace('.', '_');
    return QString("cache/recipes_%1.json").arg(versionFormatted);
}

QString RecipeRegistry::getTagCachePath(const QString &version)
{
    QString versionFormatted = version;
    versionFormatted.replace('.', '_');
    return QString("cache/tags_%1.json").arg(versionFormatted);
}

bool RecipeRegistry::downloadRecipes(const QString &version, const QString &cachePath)
{
    QString versionFormatted = version;
    versionFormatted.replace('.', '_');

    QString url = QString("https://raw.githubusercontent.com/mankool0/mc-bot-manager/refs/heads/recipe-data/recipes/%1.json")
                     .arg(versionFormatted);

    qDebug() << "Downloading recipes for version" << version << "from GitHub...";

    QNetworkAccessManager networkManager;
    QNetworkRequest request(url);
    QNetworkReply *reply = networkManager.get(request);

    // Use event loop to wait for download
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Failed to download recipes for version" << version << ":" << reply->errorString();
        reply->deleteLater();
        return false;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    // Save to cache
    QDir dir;
    if (!dir.exists("cache")) {
        dir.mkpath("cache");
    }

    QFile cacheFile(cachePath);
    if (!cacheFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to write recipe cache file:" << cachePath;
        return false;
    }

    cacheFile.write(data);
    cacheFile.close();

    qDebug() << "Downloaded recipes for version" << version;
    return true;
}

bool RecipeRegistry::downloadTags(const QString &version, const QString &cachePath)
{
    QString versionFormatted = version;
    versionFormatted.replace('.', '_');

    QString url = QString("https://raw.githubusercontent.com/mankool0/mc-bot-manager/refs/heads/recipe-data/tags/item/%1.json")
                     .arg(versionFormatted);

    qDebug() << "Downloading tags for version" << version << "from GitHub...";

    QNetworkAccessManager networkManager;
    QNetworkRequest request(url);
    QNetworkReply *reply = networkManager.get(request);

    // Use event loop to wait for download
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Failed to download tags for version" << version << ":" << reply->errorString();
        reply->deleteLater();
        return false;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    // Save to cache
    QDir dir;
    if (!dir.exists("cache")) {
        dir.mkpath("cache");
    }

    QFile cacheFile(cachePath);
    if (!cacheFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to write tag cache file:" << cachePath;
        return false;
    }

    cacheFile.write(data);
    cacheFile.close();

    qDebug() << "Downloaded tags for version" << version;
    return true;
}
