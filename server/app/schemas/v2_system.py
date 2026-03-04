from pydantic import BaseModel


class SystemVersionOutV2(BaseModel):
    server_version: str
    api_version: str = "v2"
    git_commit: str
    build_timestamp: str
