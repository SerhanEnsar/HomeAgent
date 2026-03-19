from fastapi import APIRouter, Request, Form, UploadFile, File, HTTPException
from fastapi.responses import JSONResponse, FileResponse
from pathlib import Path
import shutil
import mimetypes
import datetime
from pydantic import BaseModel

router = APIRouter(prefix="/api/files")

TRASH_DIR = Path("/home/serhanensar/.local/share/Trash/files")
TRASH_INFO_DIR = Path("/home/serhanensar/.local/share/Trash/info")
TRASH_DIR.mkdir(parents=True, exist_ok=True)
TRASH_INFO_DIR.mkdir(parents=True, exist_ok=True)

class RenameReq(BaseModel):
    mount: str
    path: str
    new_name: str

class DeleteReq(BaseModel):
    mount: str
    path: str

class MkdirReq(BaseModel):
    mount: str
    path: str
    name: str

class MoveReq(BaseModel):
    mount: str
    src: str
    dst: str

class TrashRestoreReq(BaseModel):
    name: str

class TrashDeleteReq(BaseModel):
    name: str


def _get_mounts():
    import subprocess
    out = subprocess.check_output(["df", "-h"], text=True).splitlines()
    mounts = []
    for line in out[1:]:
        parts = line.split()
        if len(parts) < 6:
            continue
        filesystem, size, used, avail, percent, mount = parts[:6]
        if filesystem == "tmpfs":
            continue
        if mount.startswith(("/proc", "/sys", "/dev", "/run", "/snap")):
            continue
        mounts.append({"filesystem": filesystem, "mount": mount, "size": size, "used": used, "avail": avail, "percent": percent})
    return mounts

def _find_mount(mount: str):
    for m in _get_mounts():
        if m["mount"] == mount:
            return m
    return None

def _resolve_in_mount(mount: str, path: str) -> Path:
    m = _find_mount(mount)
    if not m:
        raise HTTPException(status_code=400, detail="invalid_mount")
    base = Path(mount).resolve()
    rel = Path(path.lstrip("/"))
    target = (base / rel).resolve()
    if not str(target).startswith(str(base)):
        raise HTTPException(status_code=400, detail="invalid_path")
    return target


@router.get("/devices")
def api_files_devices(request: Request):
    return _get_mounts()

@router.get("/list")
def api_files_list(request: Request, mount: str, path: str = ""):
    m = _find_mount(mount)
    if not m:
        return {"error": "invalid_mount"}
    base = Path(mount).resolve()
    rel = Path(path.lstrip("/"))
    target = (base / rel).resolve()
    if not str(target).startswith(str(base)):
        return {"error": "invalid_path"}
    if not target.exists() or not target.is_dir():
        return {"error": "not_found"}
    items = []
    for p in sorted(target.iterdir(), key=lambda x: (not x.is_dir(), x.name.lower())):
        try:
            st = p.stat()
            items.append({"name": p.name, "type": "dir" if p.is_dir() else "file", "size": st.st_size if p.is_file() else None, "mtime": int(st.st_mtime)})
        except PermissionError:
            items.append({"name": p.name, "type": "unknown", "size": None, "mtime": None})
    return {"mount": mount, "path": str(rel), "items": items}

@router.get("/download")
def api_files_download(request: Request, mount: str, path: str):
    target = _resolve_in_mount(mount, path)
    if not target.exists() or not target.is_file():
        raise HTTPException(status_code=404, detail="not_found")
    mime, _ = mimetypes.guess_type(str(target))
    return FileResponse(path=str(target), media_type=mime or "application/octet-stream", filename=target.name)

@router.post("/rename")
def api_files_rename(req: RenameReq, request: Request):
    src = _resolve_in_mount(req.mount, req.path)
    if not src.exists():
        raise HTTPException(status_code=404, detail="not_found")
    if "/" in req.new_name or req.new_name.strip() == "":
        raise HTTPException(status_code=400, detail="bad_name")
    dst = src.parent / req.new_name
    if dst.exists():
        raise HTTPException(status_code=409, detail="already_exists")
    src.rename(dst)
    return {"ok": True}

@router.post("/delete")
def api_files_delete(req: DeleteReq, request: Request):
    target = _resolve_in_mount(req.mount, req.path)
    if not target.exists():
        raise HTTPException(status_code=404, detail="not_found")
    if target.is_dir():
        shutil.rmtree(target)
    else:
        target.unlink()
    return {"ok": True}

@router.post("/mkdir")
async def api_files_mkdir(req: MkdirReq, request: Request):
    target = _resolve_in_mount(req.mount, req.path + "/" + req.name)
    if target.exists():
        raise HTTPException(status_code=409, detail="already_exists")
    target.mkdir(parents=True)
    return {"ok": True}

@router.post("/upload")
async def api_files_upload(request: Request, mount: str = Form(...), path: str = Form(...), file: UploadFile = File(...)):
    target_dir = _resolve_in_mount(mount, path)
    target_file = target_dir / file.filename
    with open(target_file, "wb") as f:
        shutil.copyfileobj(file.file, f)
    return {"ok": True, "name": file.filename}

@router.post("/move")
async def api_files_move(req: MoveReq, request: Request):
    src = _resolve_in_mount(req.mount, req.src)
    dst = _resolve_in_mount(req.mount, req.dst)
    if not src.exists():
        raise HTTPException(status_code=404, detail="not_found")
    shutil.move(str(src), str(dst))
    return {"ok": True}

@router.post("/copy")
async def api_files_copy(req: MoveReq, request: Request):
    src = _resolve_in_mount(req.mount, req.src)
    dst = _resolve_in_mount(req.mount, req.dst)
    if not src.exists():
        raise HTTPException(status_code=404, detail="not_found")
    if src.is_dir():
        shutil.copytree(str(src), str(dst))
    else:
        shutil.copy2(str(src), str(dst))
    return {"ok": True}

@router.post("/trash")
async def api_files_trash(req: DeleteReq, request: Request):
    target = _resolve_in_mount(req.mount, req.path)
    if not target.exists():
        raise HTTPException(status_code=404, detail="not_found")
    dst = TRASH_DIR / target.name
    counter = 1
    while dst.exists():
        dst = TRASH_DIR / f"{target.stem}_{counter}{target.suffix}"
        counter += 1
    info = TRASH_INFO_DIR / (dst.name + ".trashinfo")
    info.write_text(f"[Trash Info]\nPath={target}\nDeletionDate={datetime.datetime.now().isoformat()}\n")
    shutil.move(str(target), str(dst))
    return {"ok": True}

@router.get("/trash/list")
def api_files_trash_list(request: Request):
    items = []
    for p in TRASH_DIR.iterdir():
        try:
            st = p.stat()
            items.append({
                "name": p.name,
                "type": "dir" if p.is_dir() else "file",
                "size": st.st_size if p.is_file() else None,
                "mtime": int(st.st_mtime)
            })
        except:
            pass
    return items

@router.post("/trash/restore")
async def api_files_trash_restore(req: TrashRestoreReq, request: Request):
    src = TRASH_DIR / req.name
    info_file = TRASH_INFO_DIR / (req.name + ".trashinfo")
    if not src.exists():
        raise HTTPException(status_code=404, detail="not_found")
    restore_path = None
    if info_file.exists():
        for line in info_file.read_text().splitlines():
            if line.startswith("Path="):
                restore_path = Path(line[5:])
                break
    if not restore_path:
        restore_path = Path("/home/serhanensar") / req.name
    shutil.move(str(src), str(restore_path))
    if info_file.exists():
        info_file.unlink()
    return {"ok": True, "restored_to": str(restore_path)}

@router.post("/trash/delete")
async def api_files_trash_delete(req: TrashDeleteReq, request: Request):
    target = TRASH_DIR / req.name
    info_file = TRASH_INFO_DIR / (req.name + ".trashinfo")
    if not target.exists():
        raise HTTPException(status_code=404, detail="not_found")
    if target.is_dir():
        shutil.rmtree(target)
    else:
        target.unlink()
    if info_file.exists():
        info_file.unlink()
    return {"ok": True}

@router.get("/preview")
def api_files_preview(request: Request, mount: str, path: str):
    target = _resolve_in_mount(mount, path)
    if not target.exists() or not target.is_file():
        raise HTTPException(status_code=404, detail="not_found")
    
    mime, _ = mimetypes.guess_type(str(target))
    
    # sadece metin dosyalarını oku
    if mime and (mime.startswith("text/") or mime in ["application/json", "application/xml"]):
        try:
            content = target.read_text(encoding="utf-8", errors="replace")
            return {"type": "text", "content": content, "mime": mime}
        except:
            raise HTTPException(status_code=400, detail="cannot_read")
    
    # resim dosyaları
    if mime and mime.startswith("image/"):
        return {"type": "image", "mime": mime}
    
    # pdf
    if mime == "application/pdf":
        return {"type": "pdf", "mime": mime}
    
    return {"type": "unsupported", "mime": mime}

@router.get("/search")
def api_files_search(request: Request, mount: str, query: str, path: str = ""):
    m = _find_mount(mount)
    if not m:
        return {"error": "invalid_mount"}
    
    base = Path(mount).resolve()
    search_root = (base / path.lstrip("/")).resolve()
    
    if not str(search_root).startswith(str(base)):
        return {"error": "invalid_path"}
    
    results = []
    query_lower = query.lower()
    
    for p in search_root.rglob("*"):
        try:
            if query_lower in p.name.lower():
                st = p.stat()
                results.append({
                    "name": p.name,
                    "path": str(p.relative_to(base)),
                    "type": "dir" if p.is_dir() else "file",
                    "size": st.st_size if p.is_file() else None,
                })
                if len(results) >= 100:
                    break
        except PermissionError:
            continue
    
    return {"query": query, "results": results}