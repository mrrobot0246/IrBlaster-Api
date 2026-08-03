import os
import secrets
import re
from pathlib import Path
from typing import Annotated

from fastapi import Depends, FastAPI, Header, HTTPException, Request
from pydantic import BaseModel, field_validator
from slowapi import Limiter, _rate_limit_exceeded_handler
from slowapi.errors import RateLimitExceeded
from slowapi.util import get_remote_address

limiter = Limiter(key_func=get_remote_address)
app = FastAPI(title="IR CODES LIBRARY")
app.state.limiter = limiter
app.add_exception_handler(RateLimitExceeded, _rate_limit_exceeded_handler)

BASE_DIR = Path(__file__).resolve().parent
FILE_NAME = BASE_DIR / "ircodes.txt"
KEY_FILE = BASE_DIR / ".api_key"

API_KEY = os.getenv("API_KEY")
if not API_KEY and KEY_FILE.exists():
    API_KEY = KEY_FILE.read_text().strip()

if not API_KEY:    
    API_KEY = secrets.token_urlsafe(32)
    KEY_FILE.write_text(API_KEY)
    print("\n" + "=" * 60)
    print("No API Key Provided In ENV, !NEW GENERATED API KEY!:")
    print(f">>> {API_KEY} <<<")
    print("Copy this key into your ESP32's config.h file!")
    print("=" * 60 + "\n")
else:
    print(f"\n Loaded API Key: {API_KEY}\n")

ALLOWED_PROTOCOLS = {"NEC", "SONY", "RC5", "RC6", "SAMSUNG", "LG", "PANASONIC", "UNKNOWN"}


ircodes = {}
following_id = 1

def verify_api_key(x_api_key: Annotated[str | None, Header()] = None):
    if x_api_key != API_KEY:
        raise HTTPException(status_code=401, detail="Invalid or missing API Key")
    return x_api_key

class IrCodeCreate(BaseModel):
    id: int | None = None
    name: str
    protocol: str
    code: str
    category: str

    @field_validator("protocol")
    @classmethod
    def validate_protocol(cls, v: str) -> str:
        v_clean = v.strip().upper()
        if v_clean not in ALLOWED_PROTOCOLS:
            raise HTTPException(
                status_code=422,
                detail=f"Unsupported protocol, Must be one of: {', '.join(ALLOWED_PROTOCOLS)}"
            )
        return v_clean


    @field_validator("code")
    @classmethod
    def validate_code(cls, v: str) -> str:
        pattern = r"^(0x|0X)?[0-9a-fA-F]+$"
        if not re.match(pattern, v.strip()):
            raise HTTPException(status_code=422, detail="Invalid Hex code format")
        return v.strip()


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


@app.post("/ircodes", dependencies=[Depends(verify_api_key)])
@limiter.limit("10/minute")
def add_ircode(request: Request, ir_code: IrCodeCreate):
    global following_id

    for entry in ircodes.values():
        if entry["code"].lower() == ir_code.code.lower():
            raise HTTPException(status_code=409, detail="Ir code already exists")

    assigned_id = ir_code.id if ir_code.id is not None else following_id

    if assigned_id in ircodes:
        raise HTTPException(status_code=400, detail=f"ID {assigned_id} is already taken")

    new_entry = {
        "id": assigned_id,
        "name": ir_code.name,
        "protocol": ir_code.protocol,
        "code": ir_code.code,
        "category": ir_code.category,


    }

    ircodes[assigned_id] = new_entry
    if assigned_id >= following_id:
        following_id = assigned_id + 1

    save_to_file()
    return new_entry

@app.get("/ircodes", dependencies=[Depends(verify_api_key)])
@limiter.limit("60/minute")
def list_ircodes(
    request: Request,
    category: str | None = None,
    skip: int = 0,
    limit: int = 100
):
    all_codes = list(ircodes.values())
    if category:
        all_codes = [c for c in all_codes if c["category"] == category]

    return all_codes[skip: skip + limit]

@app.get("/ircodes/{ircode_id}", dependencies=[Depends(verify_api_key)])
@limiter.limit("60/minute")
def get_ircode(request: Request, ircode_id: int):
    if ircode_id not in ircodes:
        raise HTTPException(status_code=404 ,detail="Ir code not found")
    return ircodes[ircode_id]