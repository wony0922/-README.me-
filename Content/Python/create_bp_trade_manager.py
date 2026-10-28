import unreal

ASSET_PATH = "/Game/Blueprints/BP_TradeManager"
PACKAGE_PATH = "/Game/Blueprints"
ASSET_NAME = "BP_TradeManager"
PARENT_CLASS_PATH = "/Script/TeamProject.OreDealTradeManager"


def run():
    if not unreal.EditorAssetLibrary.does_directory_exist(PACKAGE_PATH):
        unreal.EditorAssetLibrary.make_directory(PACKAGE_PATH)

    parent_class = unreal.load_class(None, PARENT_CLASS_PATH)
    if parent_class is None:
        raise RuntimeError(
            "Could not load " + PARENT_CLASS_PATH + ". Compile the TeamProject module first."
        )

    asset = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
    if asset is None:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", parent_class)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            ASSET_NAME, PACKAGE_PATH, unreal.Blueprint, factory
        )
        if asset is None:
            raise RuntimeError("Failed to create " + ASSET_PATH)
        unreal.log("Created " + ASSET_PATH)
    elif not isinstance(asset, unreal.Blueprint):
        raise TypeError(ASSET_PATH + " exists but is not a Blueprint asset")

    generated_class = asset.generated_class()
    if generated_class is None or not generated_class.is_child_of(parent_class):
        raise TypeError(ASSET_PATH + " does not inherit from " + PARENT_CLASS_PATH)

    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
        raise RuntimeError("Failed to save " + ASSET_PATH)

    unreal.log("SUCCESS: " + ASSET_PATH + " inherits from " + PARENT_CLASS_PATH)


run()
