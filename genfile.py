import os, sys

def create_files(path):
    files = []
    if not os.path.exists(path):
        os.makedirs(path)
    for i in xrange(20):
        filepath = os.path.join(path, "file%02d.txt" % i)
        with open(filepath, 'wb') as f:
            pass
        files.append(filepath)
        
    return files

def create_dirs( path, numbers ):
    if not numbers:
        return create_files(path)
    
    newdirs = ["dir%02d" % i for i in xrange(numbers[0])]
    
    new_files = []
    for dir in newdirs:
        new_files += create_dirs( os.path.join(path, dir), numbers[1:] )
    return new_files


files = create_dirs('tmp', [30, 10, 10, 3])
print "Created %d files" % len(files)
