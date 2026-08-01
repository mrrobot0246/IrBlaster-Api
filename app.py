from pathlib import Path
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel

app = FastAPI(title="IR CODES LIBRARY")

BASE_DIR = Path(__file__).resolve().parent
FILE_NAME = BASE_DIR / "ircodes.txt"

ircodes = {}
following_id = 1


class IrCodeCreate(BaseModel):
    id: int | None = None
    name: str
    protocol: str
    code: str
    category: str


def load_from_file():
    global following_id
    try:
        with open(FILE_NAME, "r") as f:
            max_id = 0
            for line in f:
                line = line.strip()
                if not line:
                    continue
                parts = [p.strip() for p in line.split(",")]
                if len(parts) == 5:
                    id_str, name, protocol, code, category = parts
                    entry_id = int(id_str)
                    ircodes[entry_id] = {
                        "id": entry_id,
                        "name": name,
                        "protocol": protocol,
                        "code": code,
                        "category": category,
                    }
                    if entry_id > max_id:
                        max_id = entry_id
            following_id = max_id + 1
    except FileNotFoundError:
        pass


def save_to_file():
    with open(FILE_NAME, "w") as f:
        for entry in ircodes.values():
            line = f"{entry['id']},{entry['name']},{entry['protocol']},{entry['code']},{entry['category']}\n"
            f.write(line)


load_from_file()


@app.post("/ircodes")
def add_ircode(ir_code: IrCodeCreate):
    global following_id

    for entry in ircodes.values():
        if entry["code"] == ir_code.code:
            raise HTTPException(status_code=409, detail="IR code already exists")

    assigned_id = ir_code.id if ir_code.id is not None else following_id

    if assigned_id in ircodes:
        raise HTTPException(status_code=400, detail=f"ID {assigned_id} is already taken")

    entry_data = ir_code.model_dump()
    entry_data["id"] = assigned_id

    ircodes[assigned_id] = entry_data

    if assigned_id >= following_id:
        following_id = assigned_id + 1
   
    save_to_file()
    return entry_data


@app.get("/ircodes")
def list_ircodes(category: str | None = None):
    all_codes = list(ircodes.values())
    if category:
        return [c for c in all_codes if c["category"] == category]
    return all_codes


@app.get("/ircodes/{ircode_id}")
def get_ircode(ircode_id: int):
    if ircode_id not in ircodes:
        raise HTTPException(status_code=404, detail="Ir code not found")
    return ircodes[ircode_id]