# Boardgame Component Generator

GIMP plugin allowing to generate component images based on project with xcfs files, config and assets

## Project

Project directory should have following structure:

```
project
├─config.json
├─xcfs
│ ├─some_component.xcf
│ └─another_component.xcf
├─assets
│ ├─some_asset_dir
│ │ ├─asset_1.png
│ │ └─asset_2.png
│ ├─another_asset_dir
│ │ ├─asset_1.png
│ │ └─asset_2.png
| | ...
| ...
```

### Config

Project config should have following structure:

```json
{
  "some_component": {
    "layers": {
      "type icon": "image",
      "title": "text",
      "main image": "image"
    },
    "data": [
      {
        "type icon": "some_asset_dir/asset_1.png",
        "title": "Some component title text"
      },
      {
        "type icon": "some_asset_dir/asset_1.png",
        "title": "Some component title text",
        "main image": "some_asset_dir/asset_2.png"
      }
    ] 
  },
  "another_component": {
    "layers": {
      "img1": "image",
      "img2": "image",
      "cost": "text",
      "name": "text",
      "points_background": "bool",
      "points": "text",
      "fluff": "text"
    },
    "data": [
      {
        "img1": "another_asset_dir/asset_1.png",
        "img2": "another_asset_dir/asset_2.png",
        "cost": "2",
        "name": "Farm"
      },
      {
        "cost": "10",
        "name": "Sky Tower",
        "points_background": "true",
        "points": "7",
        "fluff": "The view above the clouds is awsome"
      }
    ] 
  }
}
```