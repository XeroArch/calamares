/* === This file is part of Calamares - <https://calamares.io> ===
 *
 *   SPDX-FileCopyrightText: 2023 Adriaan de Groot <groot@kde.org>
 *   SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   Calamares is Free Software: see the License-Identifier above.
 *
 */
#include "python/PythonJob.h"

#include "CalamaresVersionX.h"
#include "GlobalStorage.h"
#include "JobQueue.h"
#include "python/Api.h"
#include "python/Logger.h"
#include "python/Pybind11Helpers.h"
#include "utils/Logger.h"

#include <QDir>
#include <QFileInfo>
#include <QString>

namespace py = pybind11;

namespace
{

static const char* s_preScript = nullptr;

QString
getPrettyNameFromScope( const py::dict& scope )
{
    static constexpr char key_name[] = "pretty_name";

    if ( scope.contains( key_name ) )
    {
        const py::object func = scope[ key_name ];
        try
        {
            const auto s = func().cast< std::string >();
            return QString::fromUtf8( s.c_str() );
        }
        catch ( const py::cast_error& )
        {
            // Ignore, we will try __doc__ next
        }
    }

    static constexpr char key_doc[] = "__doc__";
    if ( scope.contains( key_doc ) )
    {
        const py::object doc = scope[ key_doc ];
        try
        {
            const auto s = doc.cast< std::string >();
            auto string = QString::fromUtf8( s.c_str() ).trimmed();
            const auto newline_index = string.indexOf( '\n' );
            if ( newline_index >= 0 )
            {
                string.truncate( newline_index );
                return string;
            }
            // __doc__ is apparently empty, try next fallback
        }
        catch ( const py::cast_error& )
        {
            // Ignore, try next fallback
        }
    }

    // No more fallbacks
    return QString();
}

void
populate_utils( py::module_& m )
{
    m.def( "obscure", &Calamares::Python::obscure, "A function that obscures (encodes) a string" );

    m.def( "debug", &Calamares::Python::debug, "Log a debug-message" );
    m.def( "warn", &Calamares::Python::warning, "Log a warning-message" );
    m.def( "warning", &Calamares::Python::warning, "Log a warning-message" );
    m.def( "error", &Calamares::Python::error, "Log an error-message" );

    m.def( "load_yaml", &Calamares::Python::load_yaml, "Loads YAML from a file." );

    m.def( "target_env_call",
           &Calamares::Python::target_env_call,
           "Runs command in target, returns exit code.",
           py::arg( "command_list" ),
           py::arg( "input" ) = std::string(),
           py::arg( "timeout" ) = 0 );
    m.def( "check_target_env_call",
           &Calamares::Python::check_target_env_call,
           "Runs command in target, raises on error exit.",
           py::arg( "command_list" ),
           py::arg( "input" ) = std::string(),
           py::arg( "timeout" ) = 0 );
    m.def( "check_target_env_output",
           &Calamares::Python::check_target_env_output,
           "Runs command in target, returns standard output or raises on error.",
           py::arg( "command_list" ),
           py::arg( "input" ) = std::string(),
           py::arg( "timeout" ) = 0 );
    m.def( "target_env_process_output",
           &Calamares::Python::target_env_process_output,
           "Runs command in target, updating callback and returns standard output or raises on error.",
           py::arg( "command_list" ),
           py::arg( "callback" ) = pybind11::none(),
           py::arg( "input" ) = std::string(),
           py::arg( "timeout" ) = 0 );
    m.def( "host_env_process_output",
           &Calamares::Python::host_env_process_output,
           "Runs command in target, updating callback and returns standard output or raises on error.",
           py::arg( "command_list" ),
           py::arg( "callback" ) = pybind11::none(),
           py::arg( "input" ) = std::string(),
           py::arg( "timeout" ) = 0 );

    m.def( "gettext_languages",
           &Calamares::Python::gettext_languages,
           "Returns list of languages (most to least-specific) for gettext." );
    m.def( "gettext_path", &Calamares::Python::gettext_path, "Returns path for gettext search." );

    m.def( "mount",
           &Calamares::Python::mount,
           "Runs the mount utility with the specified parameters.\n"
           "Returns the program's exit code, or:\n"
           "-1 = QProcess crash\n"
           "-2 = QProcess cannot start\n"
           "-3 = bad arguments" );
}

void
populate_libcalamares( py::module_& m )
{
    m.doc() = "Calamares API for Python";

    m.add_object( "ORGANIZATION_NAME", Calamares::Python::String( CALAMARES_ORGANIZATION_NAME ) );
    m.add_object( "ORGANIZATION_DOMAIN", Calamares::Python::String( CALAMARES_ORGANIZATION_DOMAIN ) );
    m.add_object( "APPLICATION_NAME", Calamares::Python::String( CALAMARES_APPLICATION_NAME ) );
    m.add_object( "VERSION", Calamares::Python::String( CALAMARES_VERSION ) );
    m.add_object( "VERSION_SHORT", Calamares::Python::String( CALAMARES_VERSION_SHORT ) );

    auto utils = m.def_submodule( "utils", "Calamares Utility API for Python" );
    populate_utils( utils );

    py::class_< Calamares::Python::JobProxy >( m, "Job" )
        .def_readonly( "module_name", &Calamares::Python::JobProxy::moduleName )
        .def_readonly( "pretty_name", &Calamares::Python::JobProxy::prettyName )
        .def_readonly( "working_path", &Calamares::Python::JobProxy::workingPath )
        .def_readonly( "configuration", &Calamares::Python::JobProxy::configuration )
        .def( "setprogress", &Calamares::Python::JobProxy::setprogress );

    py::class_< Calamares::Python::GlobalStorageProxy >( m, "GlobalStorage" )
        .def( py::init( []( std::nullptr_t ) { return new Calamares::Python::GlobalStorageProxy( nullptr ); } ) )
        .def( "contains", &Calamares::Python::GlobalStorageProxy::contains )
        .def( "count", &Calamares::Python::GlobalStorageProxy::count )
        .def( "insert", &Calamares::Python::GlobalStorageProxy::insert )
        .def( "keys", &Calamares::Python::GlobalStorageProxy::keys )
        .def( "remove", &Calamares::Python::GlobalStorageProxy::remove )
        .def( "value", &Calamares::Python::GlobalStorageProxy::value );
}

}  // namespace

namespace Calamares
{
namespace Python
{

struct Job::Private
{
    Private( const QString& script, const QString& path, const QVariantMap& configuration )
        : scriptFile( script )
        , workingPath( path )
        , configurationMap( configuration )
    {
    }
    QString scriptFile;  // From the module descriptor
    QString workingPath;

    QVariantMap configurationMap;  // The module configuration

    QString description;  // Obtained from the Python code
};

Job::Job( const QString& scriptFile,
          const QString& workingPath,
          const QVariantMap& moduleConfiguration,
          QObject* parent )
    : ::Calamares::Job( parent )
    , m_d( std::make_unique< Job::Private >( scriptFile, workingPath, moduleConfiguration ) )
{
}

Job::~Job() {}

QString
Job::prettyName() const
{
    return QDir( m_d->workingPath ).dirName();
}

QString
Job::prettyStatusMessage() const
{
    // The description is updated when progress is reported, see emitProgress()
    if ( m_d->description.isEmpty() )
    {
        return tr( "Running %1 operation." ).arg( prettyName() );
    }
    else
    {
        return m_d->description;
    }
}

JobResult
Job::exec()
{
    // We assume m_scriptFile to be relative to m_workingPath.
    QDir workingDir( m_d->workingPath );
    if ( !workingDir.exists() || !workingDir.isReadable() )
    {
        return JobResult::error( tr( "Bad working directory path" ),
                                 tr( "Working directory %1 for python job %2 is not readable." )
                                     .arg( m_d->workingPath )
                                     .arg( prettyName() ) );
    }

    QFileInfo scriptFI( workingDir.absoluteFilePath( m_d->scriptFile ) );
    if ( !scriptFI.exists() || !scriptFI.isFile() || !scriptFI.isReadable() )
    {
        return JobResult::error( tr( "Bad main script file" ),
                                 tr( "Main script file %1 for python job %2 is not readable." )
                                     .arg( scriptFI.absoluteFilePath() )
                                     .arg( prettyName() ) );
    }

    py::scoped_interpreter guard {};
    // Import, but do not keep the handle lying around
    try
    {
        auto calamaresModule = py::module_::import( "libcalamares" );
        calamaresModule.attr( "job" ) = Calamares::Python::JobProxy( this );
        calamaresModule.attr( "globalstorage" )
            = Calamares::Python::GlobalStorageProxy( JobQueue::instance()->globalStorage() );
    }
    catch ( const py::error_already_set& e )
    {
        cError() << "Error in import:" << e.what();
        throw;  // This is non-recoverable
    }

    if ( s_preScript )
    {
        try
        {
            py::exec( s_preScript );
        }
        catch ( const py::error_already_set& e )
        {
            cError() << "Error in pre-script:" << e.what();
            return JobResult::internalError(
                tr( "Bad internal script" ),
                tr( "Internal script for python job %1 raised an exception." ).arg( prettyName() ),
                JobResult::PythonUncaughtException );
        }
    }

    try
    {
        py::eval_file( scriptFI.absoluteFilePath().toUtf8().constData() );
    }
    catch ( const py::error_already_set& e )
    {
        cError() << "Error while loading:" << e.what();
        return JobResult::internalError(
            tr( "Bad main script file" ),
            tr( "Main script file %1 for python job %2 could not be loaded because it raised an  exception." )
                .arg( scriptFI.absoluteFilePath() )
                .arg( prettyName() ),
            JobResult::PythonUncaughtException );
    }

    auto scope = py::module_::import( "__main__" ).attr( "__dict__" );
    m_d->description = getPrettyNameFromScope( scope );

    Q_EMIT progress( 0 );
    static constexpr char key_run[] = "run";
    if ( scope.contains( key_run ) )
    {
        const py::object run = scope[ key_run ];
        try
        {
            py::object r;
            try
            {
                r = run();
            }
            catch ( const py::error_already_set& e )
            {
                // This is an error in the Python code itself
                cError() << "Error while running:" << e.what();
                return JobResult::internalError( tr( "Bad main script file" ),
                                                 tr( "Main script file %1 for python job %2 raised an exception." )
                                                     .arg( scriptFI.absoluteFilePath() )
                                                     .arg( prettyName() ),
                                                 JobResult::PythonUncaughtException );
            }

            if ( r.is( py::none() ) )
            {
                return JobResult::ok();
            }
            const py::tuple items = r;
            return JobResult::error( asQString( items[ 0 ] ), asQString( items[ 1 ] ) );
        }
        catch ( const py::cast_error& e )
        {
            cError() << "Error in type of run() or its results:" << e.what();
            return JobResult::error( tr( "Bad main script file" ),
                                     tr( "Main script file %1 for python job %2 returned invalid results." )
                                         .arg( scriptFI.absoluteFilePath() )
                                         .arg( prettyName() ) );
        }
        catch ( const py::error_already_set& e )
        {
            cError() << "Error in return type of run():" << e.what();
            return JobResult::error( tr( "Bad main script file" ),
                                     tr( "Main script file %1 for python job %2 returned invalid results." )
                                         .arg( scriptFI.absoluteFilePath() )
                                         .arg( prettyName() ) );
        }
    }
    else
    {
        return JobResult::error( tr( "Bad main script file" ),
                                 tr( "Main script file %1 for python job %2 does not contain a run() function." )
                                     .arg( scriptFI.absoluteFilePath() )
                                     .arg( prettyName() ) );
    }
}

QString
Job::workingPath() const
{
    return m_d->workingPath;
}
QVariantMap
Job::configuration() const
{
    return m_d->configurationMap;
}

void
Job::emitProgress( double progressValue )
{
    // TODO: update prettyname
    emit progress( progressValue );
}

/** @brief Sets the pre-run Python code for all PythonJobs
 *
 * A PythonJob runs the code from the scriptFile parameter to
 * the constructor; the pre-run code is **also** run, before
 * even the scriptFile code. Use this in testing mode
 * to modify Python internals.
 *
 * No ownership of @p script is taken: pass in a pointer to
 * a character literal or something that lives longer than the
 * job. Pass in @c nullptr to switch off pre-run code.
 */
void
Job::setInjectedPreScript( const char* script )
{
    s_preScript = script;
    cDebug() << "Python pre-script set to string" << Logger::Pointer( script ) << "length"
             << ( script ? strlen( script ) : 0 );
}

}  // namespace Python
}  // namespace Calamares

PYBIND11_MODULE( libcalamares, m )
{
    populate_libcalamares( m );
}