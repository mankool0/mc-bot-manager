#!/usr/bin/env python3

import urllib.request
import urllib.error
import json
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed

BASE_URL = "https://raw.githubusercontent.com/InventivetalentDev/minecraft-assets/refs/heads/{version}/data/minecraft/recipe/_all.json"

VERSIONS = [
    "1.12.2",
    "1.13",
    "1.13.1",
    "1.13.2",
    "1.14",
    "1.14.1",
    "1.14.2",
    "1.14.3",
    "1.14.4",
    "1.15",
    "1.15.1",
    "1.15.2",
    "1.16",
    "1.16.1",
    "1.16.2",
    "1.16.3",
    "1.16.4",
    "1.16.5",
    "1.17",
    "1.17.1",
    "1.18",
    "1.18.1",
    "1.18.2",
    "1.19",
    "1.19.1",
    "1.19.2",
    "1.19.3",
    "1.19.4",
    "1.20",
    "1.20.1",
    "1.20.2",
    "1.20.3",
    "1.20.4",
    "1.20.5",
    "1.20.6",
    "1.21",
    "1.21.1",
    "1.21.2",
    "1.21.3",
    "1.21.4",
    "1.21.5",
    "1.21.6",
    "1.21.7",
    "1.21.8",
    "1.21.9",
    "1.21.10",
    "1.21.11",
]

OUTPUT_DIR = Path("recipes")


def download_single_recipe(version: str, base_path: str, recipe_file: str) -> tuple:
    recipe_url = f"https://raw.githubusercontent.com/InventivetalentDev/minecraft-assets/refs/heads/{version}/{base_path}/{recipe_file}"
    try:
        with urllib.request.urlopen(recipe_url) as response:
            recipe_data = json.loads(response.read().decode('utf-8'))
            recipe_name = recipe_file.replace('.json', '')
            return (f"minecraft:{recipe_name}", recipe_data)
    except:
        return None


def download_individual_recipes(version: str, base_path: str) -> dict:
    api_url = f"https://api.github.com/repos/InventivetalentDev/minecraft-assets/contents/{base_path}?ref={version}"

    try:
        with urllib.request.urlopen(api_url) as response:
            files = json.loads(response.read().decode('utf-8'))

        # Filter for .json files
        recipe_files = [f['name'] for f in files if isinstance(f, dict) and f.get('name', '').endswith('.json')]

        if not recipe_files:
            return None

        # Download each recipe file in parallel
        combined_recipes = {}
        with ThreadPoolExecutor(max_workers=32) as executor:
            futures = [executor.submit(download_single_recipe, version, base_path, recipe_file)
                      for recipe_file in recipe_files]

            for future in as_completed(futures):
                result = future.result()
                if result:
                    name, data = result
                    combined_recipes[name] = data

        return combined_recipes if combined_recipes else None
    except KeyboardInterrupt:
        raise
    except:
        return None


def download_recipe(version: str, output_dir: Path) -> bool:
    filename = version.replace('.', '_') + '.json'
    output_file = output_dir / filename

    if output_file.exists():
        print(f"{version}: Already exists, skipping")
        return True

    print(f"Downloading {version}...", end=" ", flush=True)

    base_paths = [
        "data/minecraft/recipe",
        "data/minecraft/recipes",
        "assets/minecraft/recipes",
    ]

    # Try _all.json files first in all base paths
    for base_path in base_paths:
        url = f"https://raw.githubusercontent.com/InventivetalentDev/minecraft-assets/refs/heads/{version}/{base_path}/_all.json"
        try:
            with urllib.request.urlopen(url) as response:
                data = response.read()
                recipes = json.loads(data.decode('utf-8'))

            # Ensure all recipe keys have the minecraft: prefix
            if isinstance(recipes, dict):
                normalized_recipes = {}
                for key, value in recipes.items():
                    if not key.startswith('minecraft:'):
                        normalized_recipes[f'minecraft:{key}'] = value
                    else:
                        normalized_recipes[key] = value
                recipes = normalized_recipes

            # Pretty print the JSON
            with open(output_file, 'w') as f:
                json.dump(recipes, f, indent=2)

            recipe_count = len(recipes) if isinstance(recipes, dict) else 0
            print(f"Success ({recipe_count} recipes, {len(data)} bytes)", flush=True)
            return True

        except urllib.error.HTTPError:
            continue
        except urllib.error.URLError as e:
            print(f"URL Error: {e.reason}", flush=True)
            return False
        except Exception as e:
            print(f"Error: {e}", flush=True)
            return False

    # If _all.json doesn't exist in any path, try downloading individual files
    for base_path in base_paths:
        recipes = download_individual_recipes(version, base_path)
        if recipes:
            json_str = json.dumps(recipes, indent=2)
            json_bytes = len(json_str.encode('utf-8'))

            with open(output_file, 'w') as f:
                f.write(json_str)

            print(f"Success ({len(recipes)} recipes, {json_bytes} bytes, combined from individual files)", flush=True)
            return True

    print("Failed - no recipes found", flush=True)
    return False


def main():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    print(f"Downloading recipe data to {OUTPUT_DIR}/")
    print(f"Total versions: {len(VERSIONS)}\n")

    successful = 0
    failed = 0

    try:
        for version in VERSIONS:
            if download_recipe(version, OUTPUT_DIR):
                successful += 1
            else:
                failed += 1
    except KeyboardInterrupt:
        print("\n\nDownload interrupted by user!")
        print(f"Successful: {successful}")
        print(f"Failed: {failed}")
        return

    print(f"\nDownload complete!")
    print(f"Successful: {successful}")
    print(f"Failed: {failed}")


if __name__ == "__main__":
    main()
