Configuration system
====================

Plugin architecture
-------------------

The ``configuration`` plugin exposes a common API intended for use by other
plugins, and also handles common tasks such as validation. Actual data storage
is handled by format-specific plugins.

``configuration`` API
---------------------

- ``get()``: Fetches a configuration value. Takes the following parameters:

  * ``key``: refers to a particular configuration value.
  * ``type``: A character representing the required type of the configuration
    value: ``i`` for an integer, ``d`` for a double, and ``s`` for a string.
  * ``value_out``: The location in which to store the configuration value.

  Returns a boolean value; ``true`` indicates success. ``false`` may indicate
  that a value with the given key is not present, or that it has the wrong type.

- ``add_configuration_backend``: Adds a configuration backend, responsible for
  data storage and retrieval. Takes the following parameters:

  * ``caller``: The handle of the configuration backend plugin.
  * ``name``: A human-readable format name, e.g. 'INI'.
  * ``get``: A format-specific implementation of the ``get`` function, described
    above.

  Returns a boolean value; ``true`` indicates success.

Configuration keys and values
-----------------------------

- Each key must consist of letters, digits, underscores, and dashes
  only, with the forward slash (``/``) acting as a hierarchical delimiter.
  No two forward slashes may be adjacent. A leading slash is required, and
  a trailing slash is not allowed.

- Each keys corresponds to one and only one value.

- An integral value must be within in the range [−2147483647,+2147483647],
  inclusive.

- A string must be encoded in UTF8
