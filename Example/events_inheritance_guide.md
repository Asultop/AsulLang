# std.events Inheritance Support

## Overview

The `std.events` package now fully supports class inheritance, allowing custom classes to inherit from `AsulObject` and use the signal-slot mechanism.

## Usage

### Basic Inheritance

```alang
import std.events;

// Make AsulObject available for inheritance
let AsulObject = std.events.AsulObject;

class Button <- AsulObject {
    function constructor(label) {
        this.label = label;
    }

    function click() {
        println("Button '" + this.label + "' clicked!");
        this.emit("clicked", this.label);
    }
    
    function onClicked(label) {
        println("Slot: Button '" + label + "' was clicked!");
    }
}

// Create instance and connect signals
let button = new Button("Submit");
std.events.connect(button, "clicked", button, "onClicked");
button.click();
```

### Multi-level Inheritance

Classes can inherit through multiple levels:

```alang
class Widget <- AsulObject {
    function constructor() {
        this.id = "widget";
    }
    
    function notify(message) {
        this.emit("notification", this.id, message);
    }
}

class ClickableWidget <- Widget {
    function constructor(id) {
        this.id = id;
    }
    
    function handleClick() {
        this.emit("clicked");
    }
}
```

## Implementation Details

### Constructor Chain

When creating an instance of a class that inherits from `AsulObject`:

1. **Base constructors are called first**: All constructors in the inheritance chain are called in order from base to derived
2. **Base constructors receive no arguments**: Only the most derived constructor receives the arguments passed to `new`
3. **Native handles are initialized**: The `AsulObject` constructor initializes the native event handling structure

### Native Class Detection

The interpreter automatically detects if any class in the inheritance chain is native (like `AsulObject`) and creates the appropriate instance type (`InstanceExt`) to support native handles.

## Examples

See the following example files:
- `Example/events_example.alang` - Basic std.events usage
- `Example/events_inheritance_test.alang` - Simple inheritance test
- `Example/events_button_test.alang` - Button example from documentation
- `Example/events_comprehensive_test.alang` - Multi-level inheritance and cross-object connections

## Limitations

- Base class constructors are always called with no arguments
- The inheritance syntax requires importing the parent class first (e.g., `let AsulObject = std.events.AsulObject`)
- Dotted names in inheritance declarations are not yet supported (use the import workaround above)

## Signal-Slot Methods

All classes inheriting from `AsulObject` have access to:

- `emit(signal, ...args)` - Emit a signal with optional arguments
- `receive(signal, func)` - Connect a function to receive a signal
- `std.events.connect(sender, signal, receiver, slot)` - Connect objects together

## See Also

- Qt Signal-Slot documentation for conceptual reference
- `std.events` package documentation
