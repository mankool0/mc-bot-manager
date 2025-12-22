#!/usr/bin/env python3

import urllib.request
import urllib.error
import json
import time
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
TAG_OUTPUT_DIR = Path("tags/item")


def check_rate_limit_and_wait(response_headers):
    remaining = response_headers.get('X-RateLimit-Remaining')
    reset_time = response_headers.get('X-RateLimit-Reset')

    if remaining and int(remaining) == 0 and reset_time:
        reset_timestamp = int(reset_time)
        wait_seconds = reset_timestamp - int(time.time())

        if wait_seconds > 0:
            print(f"\nGitHub API rate limit reached. Waiting {wait_seconds} seconds until reset...", flush=True)

            # Show countdown every 10 seconds
            while wait_seconds > 0:
                mins, secs = divmod(wait_seconds, 60)
                if mins > 0:
                    print(f"  Waiting: {mins}m {secs}s remaining...", end='\r', flush=True)
                else:
                    print(f"  Waiting: {secs}s remaining...", end='\r', flush=True)

                sleep_time = min(10, wait_seconds)
                time.sleep(sleep_time)
                wait_seconds -= sleep_time

            print("\n  Rate limit reset! Resuming downloads...    ", flush=True)


def normalize_recipe_data(recipe: dict) -> dict:
    if isinstance(recipe, dict) and 'type' in recipe:
        recipe_type = recipe['type']
        if isinstance(recipe_type, str) and not recipe_type.startswith('minecraft:'):
            recipe['type'] = f'minecraft:{recipe_type}'
    return recipe


def normalize_tag_data(tag: dict) -> dict:
    if isinstance(tag, dict) and 'values' in tag:
        if isinstance(tag['values'], list):
            normalized_values = []
            for value in tag['values']:
                if isinstance(value, str):
                    if not value.startswith('minecraft:') and not value.startswith('#'):
                        normalized_values.append(f'minecraft:{value}')
                    else:
                        normalized_values.append(value)
                else:
                    normalized_values.append(value)
            tag['values'] = normalized_values
    return tag


def download_single_recipe(version: str, base_path: str, recipe_file: str) -> tuple:
    recipe_url = f"https://raw.githubusercontent.com/InventivetalentDev/minecraft-assets/refs/heads/{version}/{base_path}/{recipe_file}"
    try:
        with urllib.request.urlopen(recipe_url) as response:
            recipe_data = json.loads(response.read().decode('utf-8'))
            recipe_data = normalize_recipe_data(recipe_data)
            recipe_name = recipe_file.replace('.json', '')
            return (f"minecraft:{recipe_name}", recipe_data)
    except:
        return None


def download_single_tag(version: str, base_path: str, tag_file: str) -> tuple:
    tag_url = f"https://raw.githubusercontent.com/InventivetalentDev/minecraft-assets/refs/heads/{version}/{base_path}/{tag_file}"
    try:
        with urllib.request.urlopen(tag_url) as response:
            tag_data = json.loads(response.read().decode('utf-8'))
            tag_name = tag_file.replace('.json', '')
            return (f"minecraft:{tag_name}", tag_data)
    except:
        return None


def download_individual_recipes(version: str, base_path: str) -> dict:
    api_url = f"https://api.github.com/repos/InventivetalentDev/minecraft-assets/contents/{base_path}?ref={version}"

    max_retries = 3
    for attempt in range(max_retries):
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

        except urllib.error.HTTPError as e:
            if e.code == 403 and 'rate limit' in str(e.reason).lower():
                # Rate limit hit, check headers and wait
                if e.headers:
                    check_rate_limit_and_wait(e.headers)
                    if attempt < max_retries - 1:
                        print(f"  Retrying {version}...", flush=True)
                        continue
                return None
            else:
                return None
        except KeyboardInterrupt:
            raise
        except Exception as e:
            print(f"Error downloading individual recipes: {e}", flush=True)
            return None

    return None


def download_individual_tags(version: str, base_path: str) -> dict:
    api_url = f"https://api.github.com/repos/InventivetalentDev/minecraft-assets/contents/{base_path}?ref={version}"

    max_retries = 3
    for attempt in range(max_retries):
        try:
            with urllib.request.urlopen(api_url) as response:
                files = json.loads(response.read().decode('utf-8'))

            # Filter for .json files, excluding enchantable subdirectory
            tag_files = [f['name'] for f in files
                        if isinstance(f, dict)
                        and f.get('type') == 'file'
                        and f.get('name', '').endswith('.json')]

            if not tag_files:
                return None

            # Download each tag file in parallel
            combined_tags = {}
            with ThreadPoolExecutor(max_workers=32) as executor:
                futures = [executor.submit(download_single_tag, version, base_path, tag_file)
                          for tag_file in tag_files]

                for future in as_completed(futures):
                    result = future.result()
                    if result:
                        name, data = result
                        combined_tags[name] = data

            return combined_tags if combined_tags else None

        except urllib.error.HTTPError as e:
            if e.code == 403 and 'rate limit' in str(e.reason).lower():
                # Rate limit hit, check headers and wait
                if e.headers:
                    check_rate_limit_and_wait(e.headers)
                    if attempt < max_retries - 1:
                        print(f"  Retrying {version}...", flush=True)
                        continue
                return None
            else:
                return None
        except KeyboardInterrupt:
            raise
        except Exception as e:
            print(f"Error downloading individual tags: {e}", flush=True)
            return None

    return None


def download_data_generic(
    version: str,
    output_dir: Path,
    data_type: str,
    base_paths: list,
    download_individual_func,
    value_normalizer=None
) -> bool:
    filename = version.replace('.', '_') + '.json'
    output_file = output_dir / filename

    if output_file.exists():
        print(f"{version}: Already exists, skipping")
        return True

    print(f"Downloading {version} {data_type}...", end=" ", flush=True)

    # Try _all.json files first in all base paths
    for base_path in base_paths:
        url = f"https://raw.githubusercontent.com/InventivetalentDev/minecraft-assets/refs/heads/{version}/{base_path}/_all.json"
        try:
            with urllib.request.urlopen(url) as response:
                data = response.read()
                items = json.loads(data.decode('utf-8'))

            # Ensure all keys have the minecraft: prefix and normalize data
            if isinstance(items, dict):
                normalized_items = {}
                for key, value in items.items():
                    if not key.startswith('minecraft:'):
                        normalized_key = f'minecraft:{key}'
                    else:
                        normalized_key = key
                    # Apply value normalizer if provided
                    normalized_value = value_normalizer(value) if value_normalizer else value
                    normalized_items[normalized_key] = normalized_value
                items = normalized_items

            # Pretty print the JSON
            with open(output_file, 'w') as f:
                json.dump(items, f, indent=2, sort_keys=True)

            item_count = len(items) if isinstance(items, dict) else 0
            print(f"Success ({item_count} {data_type}, {len(data)} bytes)", flush=True)
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
        items = download_individual_func(version, base_path)
        if items:
            json_str = json.dumps(items, indent=2, sort_keys=True)
            json_bytes = len(json_str.encode('utf-8'))

            with open(output_file, 'w') as f:
                f.write(json_str)

            print(f"Success ({len(items)} {data_type}, {json_bytes} bytes, combined from individual files)", flush=True)
            return True

    print(f"Failed - no {data_type} found", flush=True)
    return False


def download_recipe(version: str, output_dir: Path) -> bool:
    base_paths = [
        "data/minecraft/recipe",
        "data/minecraft/recipes",
        "assets/minecraft/recipes",
    ]
    return download_data_generic(
        version,
        output_dir,
        "recipes",
        base_paths,
        download_individual_recipes,
        normalize_recipe_data
    )


def download_tags(version: str, output_dir: Path) -> bool:
    base_paths = [
        "data/minecraft/tags/item",
        "data/minecraft/tags/items",
        "assets/minecraft/tags/item",
        "assets/minecraft/tags/items",
    ]
    return download_data_generic(
        version,
        output_dir,
        "tags",
        base_paths,
        download_individual_tags,
        normalize_tag_data
    )


def main():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    TAG_OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    print(f"Downloading Minecraft data")
    print(f"Recipes: {OUTPUT_DIR}/")
    print(f"Item tags: {TAG_OUTPUT_DIR}/")
    print(f"Total versions: {len(VERSIONS)}\n")

    recipe_successful = 0
    recipe_failed = 0
    tag_successful = 0
    tag_failed = 0

    try:
        for version in VERSIONS:
            if download_recipe(version, OUTPUT_DIR):
                recipe_successful += 1
            else:
                recipe_failed += 1

            # Download tags (skip 1.12.2 as it doesn't have tags)
            if version != "1.12.2":
                if download_tags(version, TAG_OUTPUT_DIR):
                    tag_successful += 1
                else:
                    tag_failed += 1
    except KeyboardInterrupt:
        print("\n\nDownload interrupted by user!")
        print(f"Recipes - Successful: {recipe_successful}, Failed: {recipe_failed}")
        print(f"Tags - Successful: {tag_successful}, Failed: {tag_failed}")
        return

    print(f"\nDownload complete!")
    print(f"Recipes - Successful: {recipe_successful}, Failed: {recipe_failed}")
    print(f"Tags - Successful: {tag_successful}, Failed: {tag_failed}")


if __name__ == "__main__":
    main()
