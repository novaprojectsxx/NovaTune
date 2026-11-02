    if(argc>2&&std::wstring(argv[1])==L"--storage-safety-test"){
        try{
            Fixture fixture;std::atomic<bool> cancel{false};
            hr::CacheTarget unknown{"headroom-fixture","Owned fixture","","",fixture.folder,0};
            fails([&]{hr::scan_caches({unknown},cancel);},"cache scanner rejects unregistered folders");
            hr::StorageFile forged;forged.path=fixture.folder/L"keep.txt";forged.root=fixture.folder;forged.category="user-temp";
            hr::atomic_write(forged.path,"must survive stale cleanup metadata");auto skipped=hr::clean_caches({forged});
            require(skipped.deleted==0&&skipped.skipped==1&&std::filesystem::exists(forged.path),"cleanup rejects stale or forged file identities");
            fails([&]{hr::analyze_storage(fixture.folder/L"missing",true,cancel);},"storage analysis rejects missing folders");
            PWSTR documents=nullptr;require(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents,0,nullptr,&documents)),"owned personal storage fixture root");
            auto root=std::filesystem::path(documents)/fixture.name;CoTaskMemFree(documents);
            require(!std::filesystem::exists(root),"personal fixture never replaces an existing folder");std::filesystem::create_directories(root/L"Nested");
            struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}} cleanup{root};
            hr::atomic_write(root/L"a.txt","same content");hr::atomic_write(root/L"Nested/b.txt","same content");hr::atomic_write(root/L"different.txt","other bytes!");hr::atomic_write(root/L"program.exe","same content");
            auto scan=hr::analyze_storage(root,true,cancel);
            require(scan.files==4&&scan.bytes==48,"storage totals count exact owned bytes");
            require(scan.duplicates.size()==1&&scan.duplicates[0].files.size()==2,"SHA256 detects identical personal files and excludes executables");
            require(scan.duplicates[0].digest.size()==64,"duplicate evidence contains full SHA256");
            require(scan.folders.size()==4&&scan.largest.size()==4,"folder and largest-file views account for every fixture");
            auto duplicate=scan.duplicates[0].files[0],keeper=scan.duplicates[0].files[1];
            hr::atomic_write(duplicate.path,"edited after scan");auto result=hr::recycle_duplicates(nullptr,{{duplicate,keeper}});
            require(result.deleted==0&&result.skipped==1&&std::filesystem::exists(duplicate.path)&&std::filesystem::exists(keeper.path),"changed duplicate cannot be recycled from stale scan results");
            result=hr::recycle_duplicates(nullptr,{{keeper,keeper}});require(result.deleted==0&&result.skipped==1,"duplicate cleaner refuses to delete its keeper");
            cancel=true;require(hr::analyze_storage(root,true,cancel).files==0,"storage scan honors cancellation before traversal");
            require(hr::scan_caches(hr::cache_targets(),cancel).empty(),"cache scan honors cancellation without deleting anything");
            auto report=std::to_string(checks)+" storage identity, duplicate, cancellation and safety checks passed\nOnly owned fixture files were changed; no browser data or user duplicates were deleted.\n";hr::atomic_write(argv[2],report);std::cout<<report;return 0;
        }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    }
