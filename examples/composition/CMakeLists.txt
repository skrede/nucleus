# Composing sources: explicit precedence, stacking, and reusing one space across
# many source stacks.
nucleus_add_example(layering nucleus::env nucleus::argv)
nucleus_add_example(source_stack nucleus::env nucleus::argv nucleus::runtime)
nucleus_add_example(reusable_space nucleus::runtime)
nucleus_add_example(plugin_spaces nucleus::runtime)
