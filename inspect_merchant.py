import unreal

print("=== INSPECTING BP_MERCHANT ===")
bp = unreal.load_asset('/Game/Blueprints/BP_Merchant')
if not bp:
    print("Could not load BP_Merchant")
else:
    print("Parent class:", bp.get_editor_property('parent_class'))
    
    # Check components in SCS
    scs = bp.get_editor_property('simple_construction_script')
    if scs:
        nodes = scs.get_all_nodes()
        print(f"Total SCS components: {len(nodes)}")
        for node in nodes:
            comp = node.get_component_template()
            print(f"Component: {comp.get_name()} ({comp.get_class().get_name()})")
            if isinstance(comp, unreal.StaticMeshComponent):
                sm = comp.get_editor_property('static_mesh')
                print(f"  - StaticMesh: {sm.get_name() if sm else 'None'}")
                mats = comp.get_editor_property('override_materials')
                print(f"  - Override Materials: {[m.get_name() if m else 'None' for m in mats]}")
                print(f"  - Relative Scale: {comp.get_editor_property('relative_scale3d')}")
                print(f"  - Relative Location: {comp.get_editor_property('relative_location')}")
                print(f"  - Collision Profile: {comp.get_collision_profile_name()}")
    
    # Check default actor properties
    cdo = unreal.get_default_object(bp.generated_class())
    if cdo:
        tags = cdo.get_editor_property('tags')
        print(f"Actor Tags: {tags}")

    # Check graphs
    print("Graphs:")
    for graph in bp.get_editor_property('uber_graph_pages'):
        print(f"  - UberGraph: {graph.get_name()}")
        # Check nodes
        nodes = graph.get_editor_property('nodes')
        print(f"    Node count: {len(nodes)}")
        for n in nodes:
            print(f"      * {n.get_name()} ({n.get_class().get_name()})")
