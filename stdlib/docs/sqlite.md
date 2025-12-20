# Hemlock SQLite Module

A standard library module providing SQLite3 database operations for Hemlock programs via FFI.

## Overview

The sqlite module provides comprehensive SQLite3 database functionality:

- **Connection management** - Open, close, and manage database connections
- **Query execution** - Execute SQL with parameter binding
- **Result handling** - Fetch results as arrays of objects
- **Transactions** - Begin, commit, rollback with automatic handling
- **Prepared statements** - Efficient repeated query execution
- **Schema utilities** - Table existence checks, listing, column info

## Usage

```hemlock
import { open_db, query, exec, close_db } from "@stdlib/sqlite";

let db = open_db("mydata.db");

// Create a table
exec(db, "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)");

// Insert data with parameters
exec(db, "INSERT INTO users (name, age) VALUES (?, ?)", ["Alice", 30]);
exec(db, "INSERT INTO users (name, age) VALUES (?, ?)", ["Bob", 25]);

// Query data
let users = query(db, "SELECT * FROM users WHERE age > ?", [20]);
for (user in users) {
    print(user.name + " is " + typeof(user.age) + " years old");
}

close_db(db);
```

Or import all:

```hemlock
import * as sqlite from "@stdlib/sqlite";

let db = sqlite.open_db(":memory:");
// ...
sqlite.close_db(db);
```

---

## Database Connection

### open_db(path)

Opens a database connection.

**Parameters:**
- `path: string` - Path to database file, or `:memory:` for in-memory database

**Returns:** `Database` - Database connection object

**Throws:** Exception if database cannot be opened

```hemlock
import { open_db, close_db } from "@stdlib/sqlite";

// Open a file-based database
let db = open_db("myapp.db");

// Open an in-memory database
let mem_db = open_db(":memory:");

// Always close when done
close_db(db);
close_db(mem_db);
```

### open_db_flags(path, flags)

Opens a database with specific flags.

**Parameters:**
- `path: string` - Path to database file
- `flags: i32` - Combination of open flags

**Returns:** `Database` - Database connection object

**Available flags:**
- `SQLITE_OPEN_READONLY` - Open for reading only
- `SQLITE_OPEN_READWRITE` - Open for reading and writing
- `SQLITE_OPEN_CREATE` - Create if doesn't exist (use with READWRITE)

```hemlock
import { open_db_flags, SQLITE_OPEN_READONLY } from "@stdlib/sqlite";

// Open read-only
let db = open_db_flags("data.db", SQLITE_OPEN_READONLY);
```

### close_db(db)

Closes a database connection.

**Parameters:**
- `db: Database` - Database connection to close

**Returns:** `null`

**Throws:** Exception if close fails

```hemlock
import { open_db, close_db } from "@stdlib/sqlite";

let db = open_db("test.db");
// ... use database ...
close_db(db);
```

### memory_db()

Creates an in-memory database (convenience function).

**Returns:** `Database` - In-memory database connection

```hemlock
import { memory_db, close_db } from "@stdlib/sqlite";

let db = memory_db();
// Database exists only in memory
close_db(db);
```

---

## Query Execution

### exec(db, sql, params?)

Executes a SQL statement without returning results.

**Parameters:**
- `db: Database` - Database connection
- `sql: string` - SQL statement (use `?` for parameters)
- `params: array` (optional) - Parameter values

**Returns:** `null`

**Throws:** Exception on SQL error

```hemlock
import { open_db, exec, close_db } from "@stdlib/sqlite";

let db = open_db(":memory:");

// Create table
exec(db, "CREATE TABLE products (id INTEGER PRIMARY KEY, name TEXT, price REAL)");

// Insert with parameters (prevents SQL injection)
exec(db, "INSERT INTO products (name, price) VALUES (?, ?)", ["Widget", 9.99]);
exec(db, "INSERT INTO products (name, price) VALUES (?, ?)", ["Gadget", 19.99]);

// Update
exec(db, "UPDATE products SET price = ? WHERE name = ?", [24.99, "Gadget"]);

// Delete
exec(db, "DELETE FROM products WHERE price < ?", [10.0]);

close_db(db);
```

### query(db, sql, params?)

Executes a query and returns all results.

**Parameters:**
- `db: Database` - Database connection
- `sql: string` - SQL SELECT statement
- `params: array` (optional) - Parameter values

**Returns:** `array` - Array of row objects

```hemlock
import { open_db, query, close_db } from "@stdlib/sqlite";

let db = open_db("store.db");

// Get all products
let products = query(db, "SELECT * FROM products");
for (p in products) {
    print(p.name + ": $" + typeof(p.price));
}

// Query with parameters
let expensive = query(db, "SELECT * FROM products WHERE price > ?", [50.0]);

// Query with multiple parameters
let filtered = query(db,
    "SELECT * FROM products WHERE price BETWEEN ? AND ? ORDER BY name",
    [10.0, 100.0]
);

close_db(db);
```

### query_one(db, sql, params?)

Executes a query and returns only the first row.

**Parameters:**
- `db: Database` - Database connection
- `sql: string` - SQL SELECT statement
- `params: array` (optional) - Parameter values

**Returns:** Row object or `null` if no results

```hemlock
import { open_db, query_one, close_db } from "@stdlib/sqlite";

let db = open_db("users.db");

let user = query_one(db, "SELECT * FROM users WHERE id = ?", [42]);
if (user != null) {
    print("Found: " + user.name);
} else {
    print("User not found");
}

close_db(db);
```

### query_value(db, sql, params?)

Executes a query and returns a single value (first column of first row).

**Parameters:**
- `db: Database` - Database connection
- `sql: string` - SQL SELECT statement
- `params: array` (optional) - Parameter values

**Returns:** Single value or `null`

```hemlock
import { open_db, query_value, close_db } from "@stdlib/sqlite";

let db = open_db("store.db");

// Get a count
let total = query_value(db, "SELECT COUNT(*) FROM products");
print("Total products: " + typeof(total));

// Get max price
let max_price = query_value(db, "SELECT MAX(price) FROM products");

// Get specific value
let name = query_value(db, "SELECT name FROM products WHERE id = ?", [1]);

close_db(db);
```

---

## Convenience Functions

### insert(db, sql, params?)

Inserts a row and returns the last insert rowid.

**Parameters:**
- `db: Database` - Database connection
- `sql: string` - INSERT statement
- `params: array` (optional) - Parameter values

**Returns:** `i64` - Last insert rowid

```hemlock
import { open_db, insert, close_db } from "@stdlib/sqlite";

let db = open_db("users.db");

let id = insert(db, "INSERT INTO users (name, email) VALUES (?, ?)",
    ["Alice", "alice@example.com"]);
print("Inserted user with ID: " + typeof(id));

close_db(db);
```

### update(db, sql, params?)

Updates rows and returns the number of changes.

**Parameters:**
- `db: Database` - Database connection
- `sql: string` - UPDATE statement
- `params: array` (optional) - Parameter values

**Returns:** `i32` - Number of rows affected

```hemlock
import { open_db, update, close_db } from "@stdlib/sqlite";

let db = open_db("products.db");

let changed = update(db, "UPDATE products SET price = price * 1.1 WHERE category = ?",
    ["electronics"]);
print("Updated " + typeof(changed) + " products");

close_db(db);
```

### delete_rows(db, sql, params?)

Deletes rows and returns the number of changes.

**Parameters:**
- `db: Database` - Database connection
- `sql: string` - DELETE statement
- `params: array` (optional) - Parameter values

**Returns:** `i32` - Number of rows deleted

```hemlock
import { open_db, delete_rows, close_db } from "@stdlib/sqlite";

let db = open_db("logs.db");

let deleted = delete_rows(db, "DELETE FROM logs WHERE created_at < ?",
    ["2024-01-01"]);
print("Deleted " + typeof(deleted) + " old logs");

close_db(db);
```

### count(db, table_name, where_clause?)

Counts rows in a table.

**Parameters:**
- `db: Database` - Database connection
- `table_name: string` - Name of the table
- `where_clause: string` (optional) - WHERE clause without "WHERE"

**Returns:** `i64` - Number of rows

```hemlock
import { open_db, count, close_db } from "@stdlib/sqlite";

let db = open_db("store.db");

let total = count(db, "products");
print("Total products: " + typeof(total));

let active = count(db, "products", "active = 1");
print("Active products: " + typeof(active));

close_db(db);
```

---

## Transactions

### begin(db)

Begins a transaction (DEFERRED).

```hemlock
import { open_db, begin, commit, rollback, exec, close_db } from "@stdlib/sqlite";

let db = open_db("bank.db");

begin(db);
try {
    exec(db, "UPDATE accounts SET balance = balance - 100 WHERE id = ?", [1]);
    exec(db, "UPDATE accounts SET balance = balance + 100 WHERE id = ?", [2]);
    commit(db);
} catch (e) {
    rollback(db);
    print("Transaction failed: " + e);
}

close_db(db);
```

### begin_immediate(db)

Begins an immediate transaction (acquires write lock immediately).

### begin_exclusive(db)

Begins an exclusive transaction (acquires exclusive lock).

### commit(db)

Commits the current transaction.

### rollback(db)

Rolls back the current transaction.

### transaction(db, callback)

Executes a function within a transaction with automatic commit/rollback.

**Parameters:**
- `db: Database` - Database connection
- `callback: fn` - Function to execute

**Returns:** Return value of callback

**Throws:** Re-throws callback exceptions after rollback

```hemlock
import { open_db, transaction, exec, close_db } from "@stdlib/sqlite";

let db = open_db("bank.db");

// Automatic transaction handling
let result = transaction(db, fn() {
    exec(db, "UPDATE accounts SET balance = balance - 100 WHERE id = 1");
    exec(db, "UPDATE accounts SET balance = balance + 100 WHERE id = 2");
    return "Transfer complete";
});

print(result);  // "Transfer complete"

close_db(db);
```

---

## Prepared Statements

For repeated queries, prepared statements are more efficient.

### prepare(db, sql)

Prepares a SQL statement.

**Parameters:**
- `db: Database` - Database connection
- `sql: string` - SQL statement

**Returns:** `Statement` - Prepared statement object

### stmt_exec(stmt, params?)

Executes a prepared statement (for INSERT/UPDATE/DELETE).

### stmt_query(stmt, params?)

Executes a prepared statement and returns results (for SELECT).

### stmt_finalize(stmt)

Finalizes a prepared statement (releases resources).

```hemlock
import { open_db, prepare, stmt_exec, stmt_query, stmt_finalize, close_db } from "@stdlib/sqlite";

let db = open_db(":memory:");
exec(db, "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT)");

// Prepare insert statement
let insert_stmt = prepare(db, "INSERT INTO items (name) VALUES (?)");

// Execute multiple times efficiently
let i = 0;
while (i < 1000) {
    stmt_exec(insert_stmt, ["Item " + typeof(i)]);
    i = i + 1;
}
stmt_finalize(insert_stmt);

// Prepare select statement
let select_stmt = prepare(db, "SELECT * FROM items WHERE id > ?");

let items = stmt_query(select_stmt, [990]);
print("Found " + typeof(items.length) + " items");

stmt_finalize(select_stmt);
close_db(db);
```

---

## Schema Utilities

### table_exists(db, table_name)

Checks if a table exists.

**Parameters:**
- `db: Database` - Database connection
- `table_name: string` - Name of the table

**Returns:** `bool` - True if table exists

```hemlock
import { open_db, table_exists, exec, close_db } from "@stdlib/sqlite";

let db = open_db("app.db");

if (!table_exists(db, "users")) {
    exec(db, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)");
}

close_db(db);
```

### list_tables(db)

Lists all tables in the database.

**Returns:** `array` - Array of table names

```hemlock
import { open_db, list_tables, close_db } from "@stdlib/sqlite";

let db = open_db("app.db");

let tables = list_tables(db);
for (table in tables) {
    print("Table: " + table);
}

close_db(db);
```

### table_info(db, table_name)

Gets column information for a table.

**Returns:** `array` - Array of column info objects

```hemlock
import { open_db, table_info, close_db } from "@stdlib/sqlite";

let db = open_db("app.db");

let columns = table_info(db, "users");
for (col in columns) {
    print(col.name + " (" + col.type + ")");
}

close_db(db);
```

---

## Database Info

### last_insert_id(db)

Gets the rowid of the last inserted row.

**Returns:** `i64` - Last insert rowid

### changes(db)

Gets the number of rows changed by the last statement.

**Returns:** `i32` - Number of changes

### total_changes(db)

Gets total changes since connection opened.

**Returns:** `i32` - Total number of changes

### db_error(db)

Gets the last error message.

**Returns:** `string` - Error message

### db_error_code(db)

Gets the last error code.

**Returns:** `i32` - Error code

### sqlite_version()

Gets the SQLite library version string.

**Returns:** `string` - Version string (e.g., "3.39.2")

### sqlite_version_number()

Gets the SQLite library version number.

**Returns:** `i32` - Version number (e.g., 3039002)

---

## Data Types

SQLite types are mapped to Hemlock types as follows:

| SQLite Type | Hemlock Type |
|-------------|--------------|
| INTEGER     | `i64`        |
| REAL        | `f64`        |
| TEXT        | `string`     |
| BLOB        | `buffer`     |
| NULL        | `null`       |

When binding parameters, Hemlock types are mapped as:

| Hemlock Type | SQLite Type |
|--------------|-------------|
| `null`       | NULL        |
| `bool`       | INTEGER (0/1) |
| `i8`, `i16`, `i32`, `integer` | INTEGER |
| `i64`        | INTEGER (64-bit) |
| `u8`, `u16`, `u32` | INTEGER |
| `u64`        | INTEGER (64-bit) |
| `f32`, `f64`, `number` | REAL |
| `string`     | TEXT        |
| `buffer`     | BLOB        |

---

## Examples

### CRUD Operations

```hemlock
import { open_db, exec, query, query_one, insert, update, delete_rows, close_db } from "@stdlib/sqlite";

let db = open_db("todo.db");

// Create table
exec(db, "CREATE TABLE IF NOT EXISTS todos (id INTEGER PRIMARY KEY, task TEXT, done INTEGER DEFAULT 0)");

// Create
let id = insert(db, "INSERT INTO todos (task) VALUES (?)", ["Buy groceries"]);
print("Created todo #" + typeof(id));

// Read all
let todos = query(db, "SELECT * FROM todos");
for (todo in todos) {
    let status = "pending";
    if (todo.done == 1) {
        status = "done";
    }
    print("[" + status + "] " + todo.task);
}

// Read one
let todo = query_one(db, "SELECT * FROM todos WHERE id = ?", [id]);
if (todo != null) {
    print("Found: " + todo.task);
}

// Update
let changed = update(db, "UPDATE todos SET done = 1 WHERE id = ?", [id]);
print("Marked " + typeof(changed) + " todo(s) as done");

// Delete
let deleted = delete_rows(db, "DELETE FROM todos WHERE done = 1");
print("Deleted " + typeof(deleted) + " completed todo(s)");

close_db(db);
```

### Batch Insert with Transaction

```hemlock
import { open_db, exec, begin, commit, prepare, stmt_exec, stmt_finalize, close_db } from "@stdlib/sqlite";

let db = open_db("data.db");
exec(db, "CREATE TABLE IF NOT EXISTS logs (id INTEGER PRIMARY KEY, message TEXT, level TEXT)");

// Batch insert with transaction for performance
begin(db);

let stmt = prepare(db, "INSERT INTO logs (message, level) VALUES (?, ?)");

let i = 0;
while (i < 10000) {
    stmt_exec(stmt, ["Log message " + typeof(i), "INFO"]);
    i = i + 1;
}

stmt_finalize(stmt);
commit(db);

print("Inserted 10000 log entries");
close_db(db);
```

### Join Queries

```hemlock
import { open_db, exec, query, close_db } from "@stdlib/sqlite";

let db = open_db(":memory:");

// Create tables
exec(db, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)");
exec(db, "CREATE TABLE orders (id INTEGER PRIMARY KEY, user_id INTEGER, total REAL)");

// Insert data
exec(db, "INSERT INTO users VALUES (1, 'Alice'), (2, 'Bob')");
exec(db, "INSERT INTO orders VALUES (1, 1, 99.99), (2, 1, 49.99), (3, 2, 149.99)");

// Join query
let results = query(db, "
    SELECT u.name, SUM(o.total) as total_spent
    FROM users u
    JOIN orders o ON u.id = o.user_id
    GROUP BY u.id
    ORDER BY total_spent DESC
");

for (row in results) {
    print(row.name + " spent $" + typeof(row.total_spent));
}

close_db(db);
```

### Using Blobs

```hemlock
import { open_db, exec, query, close_db } from "@stdlib/sqlite";

let db = open_db(":memory:");
exec(db, "CREATE TABLE files (id INTEGER PRIMARY KEY, name TEXT, data BLOB)");

// Create a buffer
let data = buffer(4);
data[0] = 0x89;
data[1] = 0x50;
data[2] = 0x4E;
data[3] = 0x47;

// Insert blob
exec(db, "INSERT INTO files (name, data) VALUES (?, ?)", ["image.png", data]);

// Read blob
let rows = query(db, "SELECT * FROM files WHERE name = ?", ["image.png"]);
if (rows.length > 0) {
    let file_data = rows[0].data;
    print("Retrieved " + typeof(file_data.length) + " bytes");
}

close_db(db);
```

---

## Error Handling

All functions throw exceptions on errors:

```hemlock
import { open_db, exec, close_db } from "@stdlib/sqlite";

try {
    let db = open_db("readonly.db");

    try {
        exec(db, "INVALID SQL SYNTAX");
    } catch (e) {
        print("SQL Error: " + e);
    }

    close_db(db);
} catch (e) {
    print("Database Error: " + e);
}
```

---

## Constants

### Result Codes

```hemlock
SQLITE_OK          // 0 - Success
SQLITE_ERROR       // 1 - Generic error
SQLITE_BUSY        // 5 - Database is busy
SQLITE_LOCKED      // 6 - Table is locked
SQLITE_CONSTRAINT  // 19 - Constraint violation
SQLITE_MISMATCH    // 20 - Data type mismatch
SQLITE_ROW         // 100 - sqlite3_step has another row
SQLITE_DONE        // 101 - sqlite3_step finished
```

### Column Types

```hemlock
SQLITE_INTEGER  // 1
SQLITE_FLOAT    // 2
SQLITE_TEXT     // 3
SQLITE_BLOB     // 4
SQLITE_NULL     // 5
```

### Open Flags

```hemlock
SQLITE_OPEN_READONLY   // Open read-only
SQLITE_OPEN_READWRITE  // Open read-write
SQLITE_OPEN_CREATE     // Create if not exists
```

---

## Performance Tips

1. **Use transactions for bulk operations** - Wrap multiple inserts/updates in `begin()`/`commit()`
2. **Use prepared statements** - For repeated queries, prepare once and execute many times
3. **Use parameter binding** - Never concatenate values into SQL strings
4. **Close connections** - Always close databases when done
5. **Use `:memory:`** - For temporary data, use in-memory databases

---

## Security Notes

1. **Always use parameter binding** - Never build SQL with string concatenation
2. **Validate table/column names** - Parameters can't be used for identifiers
3. **Check return values** - Handle errors appropriately
4. **Close connections** - Prevent resource leaks

```hemlock
// GOOD - Parameter binding
exec(db, "SELECT * FROM users WHERE name = ?", [user_input]);

// BAD - SQL injection vulnerability
exec(db, "SELECT * FROM users WHERE name = '" + user_input + "'");
```

---

## System Requirements

- SQLite3 shared library (`libsqlite3.so.0`)
- On Debian/Ubuntu: `sudo apt-get install libsqlite3-dev`
- On Fedora/RHEL: `sudo dnf install sqlite-devel`
- On macOS: Usually pre-installed

---

## See Also

- [SQLite Documentation](https://www.sqlite.org/docs.html)
- `@stdlib/fs` - File system operations
- `@stdlib/json` - JSON parsing for data export

---

## License

Part of the Hemlock standard library.
