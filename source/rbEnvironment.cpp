// -*- mode: C++; coding: utf-8; -*-
#include <algorithm>
#if defined(__APPLE__) || defined(__linux__) || defined(__CYGWIN__)
# include <tr1/functional>
#else
# include <functional>
#endif

#include <RigidBox/rbCollision.h>
#include <RigidBox/rbEnvironment.h>
#include <RigidBox/rbMath.h>
#include <RigidBox/rbRigidBody.h>
#include <RigidBox/rbSolver.h>

rbEnvironment::rbEnvironment()
    : bodies()
    , contacts()
    , solver()
    , config()
{
    Config default_config;
    bodies.reserve( default_config.RigidBodyCapacity );
    contacts.reserve( default_config.ContactCapacty );
    this->config = default_config;
}

rbEnvironment::rbEnvironment( const Config& config )
    : bodies()
    , contacts()
    , solver()
    , config()
{
    bodies.reserve( config.RigidBodyCapacity );
    contacts.reserve( config.ContactCapacty );
    this->config = config;
}

rbEnvironment::~rbEnvironment()
{
    if ( !bodies.empty() )
    {
        BodyPtrContainer::iterator it_end = bodies.end();
        for ( BodyPtrContainer::iterator it_current = bodies.begin(); it_current != it_end; ++it_current )
        {
            delete *it_current;
        }
        bodies.clear();
    }
}


bool rbEnvironment::Register( rbRigidBody* box )
{
    BodyPtrContainer::iterator it = std::find( bodies.begin(), bodies.end(), box );

    if ( it == bodies.end() )
    {
        bodies.push_back( box );
        return true;
    }

    return false;
}

bool rbEnvironment::Unregister( rbRigidBody* box )
{
    BodyPtrContainer::iterator it = std::find( bodies.begin(), bodies.end(), box );

    if ( it != bodies.end() )
    {
        bodies.erase( it );
        return true;
    }

    return false;
}


struct SameContacts : public std::binary_function<const rbContact&, const rbContact&, bool>
{
    static const rbReal NearThreshold = rbReal(0.02);

    bool operator()( const rbContact& c0, const rbContact& c1 ) const
    {
        return (c0.Position - c1.Position).LengthSq() <= NearThreshold ? true : false;
    }
};


void rbEnvironment::Update( rbReal dtime, int div )
{
    rbReal dt = dtime / div;

    BodyPtrContainer::iterator it_body, it_body0, it_body1, it_bodies_end = bodies.end();

    // 前処理
    contacts.clear(); // 衝突点配列をゼロクリア

    for ( rbs32 i = 0; i < div; ++i )
    {
        for ( it_body = bodies.begin(); it_body != it_bodies_end; ++it_body )
        {
            (*it_body)->ClearSolverWorkArea();   // 衝突応答で利用する一時領域をゼロクリア
            (*it_body)->UpdateInvInertiaWorld(); // 位置が更新されているため慣性テンソルも更新
        }

        // 衝突検出
        for ( it_body0 = bodies.begin(); it_body0 != it_bodies_end; ++it_body0 )
        {
            for ( it_body1 = it_body0 + 1; it_body1 != it_bodies_end; ++it_body1 )
            {
                // 壁/床同士の衝突判定は不要
                if ( (*it_body0)->IsFixed() && (*it_body1)->IsFixed() )
                    continue;

                rbContact c;
                if ( rbCollision::Detect( *it_body0, *it_body1, &c ) > 0 )
                {
                    // すでに似た衝突点が検出済みである場合は登録しない
                    ContactContainer::iterator it = std::find_if( contacts.begin(), contacts.end(),
                        std::tr1::bind(SameContacts(), c, std::tr1::placeholders::_1) );
                    if ( it == contacts.end() )
                        contacts.push_back( c );
                }
            }
        }

        // 積分 (力→速度)
        for ( it_body = bodies.begin(); it_body != it_bodies_end; ++it_body )
            (*it_body)->UpdateVelocity( dt );

        // 衝突応答
        ContactContainer::iterator it_contacts_end = contacts.end();
        for ( ContactContainer::iterator it_contact = contacts.begin(); it_contact != it_contacts_end; ++it_contact )
            solver.ApplyImpulse( &(*it_contact), dt );

        for ( it_body = bodies.begin(); it_body != it_bodies_end; ++it_body )
            (*it_body)->CorrectVelocity();

        // スリープ状態の更新
        for ( it_body = bodies.begin(); it_body != it_bodies_end; ++it_body )
        {
            (*it_body)->UpdateSleepStatus( dt );
            if ( (*it_body)->Sleeping() )
            {
                (*it_body)->SetLinearVelocity( 0, 0, 0 );
                (*it_body)->SetAngularVelocity( 0, 0, 0 ); // Angular Momentum も内部でゼロクリアされる
            }
        }

        // 積分 (速度→位置)
        for ( it_body = bodies.begin(); it_body != it_bodies_end; ++it_body )
            (*it_body)->UpdatePosition( dt );
    }

    // 後処理
    for ( it_body = bodies.begin(); it_body != it_bodies_end; ++it_body )
    {
        (*it_body)->SetForce( 0, 0, 0 );
        (*it_body)->SetTorque( 0, 0, 0 );
    }
}