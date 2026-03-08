# Plan: Fix Codegen Validations and Build System

## Issue 1: Java/Python codegen missing setter/getter/validation parity with C++

### Current State

**C++ backend** generates for each struct/message field:
- `foo()` - const getter (by value for bool/enum, by const ref otherwise)
- `mutable_foo()` - mutable reference accessor
- `set_foo(v)` - setter with constraint validation; returns `conduit::VoidResult` when constrained, `void` otherwise
  - Checks `constraint equals`, `constraint max`, `constraint min` (skips `min="0"` for unsigned)
  - Checks `max_length` for string/bytes fields
  - Skips deferred constraints (validate="deferred")
- `foo_raw()` / `set_foo_raw(v)` - for scaled fields (expose underlying integer)

**Java backend** currently:
- Uses bare `public` fields (e.g., `public int value = 0;`) — NO getters/setters at all
- Has `validate()` method that checks constraints but only as a manual call
- Has NO setter-level constraint enforcement
- Has NO raw accessors for scaled fields

**Python backend** currently:
- Uses bare instance attributes (`self.value = 0`) — NO property-based getters/setters
- Has `validate()` method that checks constraints but only as a manual call
- Has NO setter-level constraint enforcement
- Has NO raw accessors for scaled struct fields (types do have raw, but struct fields don't)

### Changes

#### A. Java backend (`java_backend.cpp`) — in `generate_j_class()`

After emitting public fields, add accessor methods:

1. **For each field**, emit:
   - `public <Type> get<Name>() { return <field>; }` — getter
   - `public void set<Name>(<Type> v) { <field> = v; }` — plain setter (no constraints)
   - OR validated setter that throws `ConduitCodecException` on constraint violation

2. **Constraint checks in setters** (matching C++ `emit_setter_constraint_checks`):
   - `constraint equals`: `if (v != <val>) throw new ConduitCodecException(...);`
   - `constraint max`: `if (v > <val>) throw new ConduitCodecException(...);`
   - `constraint min`: `if (v < <val>) throw new ConduitCodecException(...);` — skip when `min="0"` and unsigned
   - `max_length`: `if (v.length() > N) throw ...` (string) or `if (v.length > N) throw ...` (byte[])
   - Skip deferred constraints (validate="deferred")
   - For byte[] fields with constraints: convert to numeric before checking

3. **Raw accessors for scaled fields**:
   - `public <RawType> get<Name>Raw() { ... }`
   - `public void set<Name>Raw(<RawType> v) { ... }`

4. **Optional field accessors** (FX/bitmap-controlled nullable fields):
   - `public boolean has<Name>() { return <field> != null; }`
   - `public void clear<Name>() { <field> = null; }`

#### B. Python backend (`python_backend.cpp`) — in `generate_py_class()`

After emitting `__init__`, add setter methods (not properties, to keep backward compat):

1. **For each constrained field**, emit:
   - `def set_<name>(self, v)` — setter with constraint validation
   - Raises `ConstraintError` on violation

2. **Raw accessors for scaled fields**:
   - `def get_<name>_raw(self)` / `def set_<name>_raw(self, v)`

3. **Optional field helpers**:
   - `def has_<name>(self) -> bool`
   - `def clear_<name>(self) -> None`

### Tracking constraint info through field collection

Add constraint/scale info to `JFieldDef` and `PyFieldDef` structs. Populate from source `model::Field`.

---

## Issue 2: CMake doesn't build conduit-java JAR or Java tests

### Changes

In `conduit/CMakeLists.txt`:

1. **Add option `CONDUIT_BUILD_JAVA_JAR`** (default OFF) that:
   - Finds `javac` and `jar` executables
   - Compiles `conduit/bindings/java/src/main/java/io/conduit/*.java` into class files
   - Packages them into `conduit-java-0.1.0.jar`
   - Optionally installs to local Maven repo

2. **Add Java test targets** in `conduit/tests/CMakeLists.txt`:
   - Use bgen to generate Java test code from fixture BMDL files
   - Compile generated Java + test harness against conduit-java JAR
   - Add CTest targets to run Java tests

---

## Issue 3: No automated bgen codegen in Maven

### Changes

#### A. Maven (`pom.xml`) — add `exec-maven-plugin` for bgen

Add executions during `generate-sources` phase in `xcvr-java/pom.xml` and `xcvr-java11/pom.xml`.

#### B. Gradle — wire `generateCode` into `compileJava`

Add `compileJava.dependsOn generateCode` to `xcvr-java/build.gradle`.

---

## Implementation Order

1. Java codegen accessors & validations (java_backend.cpp)
2. Python codegen accessors & validations (python_backend.cpp)
3. Regenerate test fixtures for Java and Python
4. CMake Java JAR build (CMakeLists.txt)
5. Maven bgen integration (pom.xml files)
6. Gradle compileJava dependency (build.gradle)
