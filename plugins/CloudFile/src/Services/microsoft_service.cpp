#include "..\stdafx.h"
#include "microsoft_api.h"

COneDriveService::COneDriveService(HNETLIBUSER hConnection)
	: CCloudService(hConnection)
{
}

const char* COneDriveService::GetModule() const
{
	return "Microsoft";
}

const wchar_t* COneDriveService::GetText() const
{
	return LPGENW("OneDrive");
}

int COneDriveService::GetIconId() const
{
	return IDI_ONEDRIVE;
}

bool COneDriveService::IsLoggedIn()
{
	ptrA token(db_get_sa(NULL, GetModule(), "TokenSecret"));
	if (!token || token[0] == 0)
		return false;
	time_t now = time(NULL);
	time_t expiresIn = db_get_dw(NULL, GetModule(), "ExpiresIn");
	return now < expiresIn;
}

void COneDriveService::Login()
{
	ptrA token(db_get_sa(NULL, GetModule(), "TokenSecret"));
	ptrA refreshToken(db_get_sa(NULL, GetModule(), "RefreshToken"));
	if (token && refreshToken && refreshToken[0]) 	{
		OneDriveAPI::RefreshTokenRequest request(refreshToken);
		NLHR_PTR response(request.Send(hConnection));
		
		JSONNode root = GetJsonResponse(response);

		JSONNode node = root.at("access_token");
		db_set_s(NULL, GetModule(), "TokenSecret", node.as_string().c_str());

		node = root.at("expires_in");
		time_t expiresIn = time(NULL) + node.as_int();
		db_set_dw(NULL, GetModule(), "ExpiresIn", expiresIn);
		
		return;
	}
	
	COAuthDlg dlg(this, MS_OAUTH "/authorize?response_type=code&scope=offline_access%20https%3A%2F%2Fgraph.microsoft.com%2Ffiles.readWrite&redirect_uri=https%3A%2F%2Flogin.microsoftonline.com%2Fcommon%2Foauth2%2Fnativeclient&client_id=" MS_APP_ID, RequestAccessTokenThread);
	dlg.DoModal();
}

void COneDriveService::Logout()
{
	db_unset(NULL, GetModule(), "TokenSecret");
	db_unset(NULL, GetModule(), "ExpiresIn");
	db_unset(NULL, GetModule(), "RefreshToken");
}

unsigned COneDriveService::RequestAccessTokenThread(void *owner, void *param)
{
	HWND hwndDlg = (HWND)param;
	COneDriveService *service = (COneDriveService*)owner;

	if (service->IsLoggedIn())
		service->Logout();

	char requestToken[128];
	GetDlgItemTextA(hwndDlg, IDC_OAUTH_CODE, requestToken, _countof(requestToken));

	OneDriveAPI::GetAccessTokenRequest request(requestToken);
	NLHR_PTR response(request.Send(service->hConnection));

	if (response == NULL || response->resultCode != HTTP_CODE_OK) {
		const char *error = response->dataLength
			? response->pData
			: service->HttpStatusToError(response->resultCode);

		Netlib_Logf(service->hConnection, "%s: %s", service->GetModule(), error);
		ShowNotification(TranslateT("server does not respond"), MB_ICONERROR);
		return 0;
	}

	JSONNode root = JSONNode::parse(response->pData);
	if (root.empty()) {
		Netlib_Logf(service->hConnection, "%s: %s", service->GetModule(), service->HttpStatusToError(response->resultCode));
		ShowNotification(TranslateT("server does not respond"), MB_ICONERROR);
		return 0;
	}

	JSONNode node = root.at("error_description");
	if (!node.isnull()) {
		ptrW error_description(mir_a2u_cp(node.as_string().c_str(), CP_UTF8));
		Netlib_Logf(service->hConnection, "%s: %s", service->GetModule(), service->HttpStatusToError(response->resultCode));
		ShowNotification((wchar_t*)error_description, MB_ICONERROR);
		return 0;
	}

	node = root.at("access_token");
	db_set_s(NULL, service->GetModule(), "TokenSecret", node.as_string().c_str());

	node = root.at("expires_in");
	time_t expiresIn = time(NULL) + node.as_int();
	db_set_dw(NULL, service->GetModule(), "ExpiresIn", expiresIn);

	node = root.at("refresh_token");
	db_set_s(NULL, service->GetModule(), "RefreshToken", node.as_string().c_str());

	SetDlgItemTextA(hwndDlg, IDC_OAUTH_CODE, "");

	EndDialog(hwndDlg, 1);

	return 0;
}

void COneDriveService::HandleJsonError(JSONNode &node)
{
	JSONNode error = node.at("error");
	if (!error.isnull()) {
		json_string tag = error.at("message").as_string();
		throw Exception(tag.c_str());
	}
}

void COneDriveService::UploadFile(const char *parentId, const char *name, const char *data, size_t size, char *fileId)
{
	ptrA token(db_get_sa(NULL, GetModule(), "TokenSecret"));
	OneDriveAPI::UploadFileRequest *request = mir_strlen(parentId)
		? new OneDriveAPI::UploadFileRequest(token, parentId, name, data, size)
		: new OneDriveAPI::UploadFileRequest(token, name, data, size);
	NLHR_PTR response(request->Send(hConnection));
	delete request;

	JSONNode root = GetJsonResponse(response);
	JSONNode node = root.at("id");
	mir_strcpy(fileId, node.as_string().c_str());
}

void COneDriveService::CreateUploadSession(char *uploadUri, const char *name, const char *parentId)
{
	ptrA token(db_get_sa(NULL, GetModule(), "TokenSecret"));
	OneDriveAPI::CreateUploadSessionRequest *request = mir_strlen(parentId)
		? new OneDriveAPI::CreateUploadSessionRequest(token, parentId, name)
		: new OneDriveAPI::CreateUploadSessionRequest(token, name);
	NLHR_PTR response(request->Send(hConnection));
	delete request;

	JSONNode root = GetJsonResponse(response);
	JSONNode node = root.at("uploadUrl");
	mir_strcpy(uploadUri, node.as_string().c_str());
}

void COneDriveService::UploadFileChunk(const char *uploadUri, const char *chunk, size_t chunkSize, uint64_t offset, uint64_t fileSize, char *itemId)
{
	OneDriveAPI::UploadFileChunkRequest request(uploadUri, chunk, chunkSize, offset, fileSize);
	NLHR_PTR response(request.Send(hConnection));

	if (response == NULL)
		throw Exception(HttpStatusToError());

	if (response->resultCode == HTTP_CODE_ACCEPTED)
		return;

	if (HTTP_CODE_SUCCESS(response->resultCode)) {
		JSONNode root = GetJsonResponse(response);
		JSONNode node = root.at("id");
		mir_strcpy(itemId, node.as_string().c_str());
		return;
	}

	if (response->dataLength)
		throw Exception(response->pData);
	throw Exception(HttpStatusToError(response->resultCode));
}

void COneDriveService::CreateFolder(const char *path, char *itemId)
{
	ptrA token(db_get_sa(NULL, GetModule(), "TokenSecret"));
	OneDriveAPI::CreateFolderRequest request(token, path);
	NLHR_PTR response(request.Send(hConnection));

	JSONNode root = GetJsonResponse(response);
	JSONNode node = root.at("id");
	mir_strcpy(itemId, node.as_string().c_str());
}

void COneDriveService::CreateSharedLink(const char *itemId, char *url)
{
	ptrA token(db_get_sa(NULL, GetModule(), "TokenSecret"));
	OneDriveAPI::CreateSharedLinkRequest request(token, itemId);
	NLHR_PTR response(request.Send(hConnection));

	JSONNode root = GetJsonResponse(response);
	JSONNode node = root.at("link");
	JSONNode webUrl = node.at("webUrl");
	mir_strcpy(url, webUrl.as_string().c_str());
}

UINT COneDriveService::Upload(FileTransferParam *ftp)
{
	try {
		if (!IsLoggedIn())
			Login();

		if (!IsLoggedIn()) {
			ftp->SetStatus(ACKRESULT_FAILED);
			return ACKRESULT_FAILED;
		}

		char folderId[32] = { 0 };
		const wchar_t *folderName = ftp->GetFolderName();
		if (folderName) {
			char path[MAX_PATH], link[MAX_PATH];
			PreparePath(folderName, path);
			CreateFolder(path, folderId);
			CreateSharedLink(path, link);
			ftp->AppendFormatData(L"%s\r\n", ptrW(mir_utf8decodeW(link)));
		}

		ftp->FirstFile();
		do
		{
			const wchar_t *fileName = ftp->GetCurrentRelativeFilePath();
			uint64_t fileSize = ftp->GetCurrentFileSize();

			uint64_t offset = 0;
			char fileId[32];

			size_t chunkSize = ftp->GetCurrentFileChunkSize();
			mir_ptr<char>chunk((char*)mir_calloc(chunkSize));
			
			if (chunkSize == fileSize)
			{
				ftp->CheckCurrentFile();

				size_t size = ftp->ReadCurrentFile(chunk, chunkSize);

				UploadFile(folderId, T2Utf(fileName), chunk, size, fileId);
			}
			else
			{
				char uploadUri[1024];
				CreateUploadSession(uploadUri, T2Utf(fileName), folderId);

				size_t size = 0;
				for (size_t i = 0; i < (fileSize / chunkSize); i++)
				{
					ftp->CheckCurrentFile();

					size = ftp->ReadCurrentFile(chunk, chunkSize);
					if (size == 0)
						break;

					UploadFileChunk(uploadUri, chunk, size, offset, fileSize, fileId);

					offset += size;
					ftp->Progress(size);
				}
			}

			if (!wcschr(fileName, L'\\')) {
				char url[MAX_PATH];
				CreateSharedLink(fileId, url);
				ftp->AppendFormatData(L"%s\r\n", ptrW(mir_utf8decodeW(url)));
			}
		} while (ftp->NextFile());
	}
	catch (Exception &ex) {
		Netlib_Logf(hConnection, "%s: %s", MODULE, ex.what());
		ftp->SetStatus(ACKRESULT_FAILED);
		return ACKRESULT_FAILED;
	}

	ftp->SetStatus(ACKRESULT_SUCCESS);
	return ACKRESULT_SUCCESS;
}
