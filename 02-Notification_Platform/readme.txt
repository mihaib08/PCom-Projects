/**
 * Mihai-Eugen Barbu [2020-2021]
 * 325CA
 */

==== PC - Tema 2 - Aplicatie Client-Server ====

<> Trimiterea si primirea mesajelor de catre server - server.cpp,
			        respctiv subscriber - subscriber.cpp

   se realizeaza cu ajutorul structurilor (definite in helpers.h):

   >> sub_msg - mesaj trimis de catre un subscriber TCP la server care contine:

	 -> type    = 1 - subscribe
                    = 0 - unsubscribe
         -> topic[]
         -> sf      = 0/1

   >> udp_msg - mesaj primit de catre server de la clientii UDP, continand:

	 -> topic
         -> d_type     = 0 - INT
                       = 1 - SHORT_REAL
	   	       = 2 - FLOAT
	 	       = 3 - STRING
         -> payload[]  = mesajul propriu-zis

   >> tcp_msg - mesaj transmis de la server catre clientii TCP
                cu informatiile primite de la clientii UDP

	 -> udp_ip[]     - id-ul clientului UDP de la care provine mesajul
         -> prt          - portul pe care clientul UDP <udp_ip> a transmis mesajul
	 -> topic[]
	 -> d_type[]     - tipul de date al mesajului conform informatiei din udp_msg (d_type)
         -> payload[]    - conversia informatiei din udp_msg (payload)
                           corespunzatoare lui d_type

<> Pornind de la structurile descrise mai sus, am implementat logica client-server astfel:

     >> Client - subscriber.cpp

         * se realizeaza conexiunea la server cu ajutorul informatiilor primite ca argumente
         * se trimite catre server id-ul clientului curent

         * se multiplexeaza intrarea standard - stdin - si
           socket-ul catre server - sockfd

         * se verifica la fiecare moment de timp pe care din cei doi socketi se primesc date

	 	 -> daca se primesc de la stdin --> se parseaza comanda de la client folosind
                                                    (sub_msg) m

                 -> daca se primesc de la server
                               --> se parseaza mesajul primit folosind
                                   (tcp_msg *) t_msg

                               --> daca nu se primesc date (ret = recv(...) == 0)
                                        - serverul a inchis conexiunea


     >> Server - server.cpp

        > am folosit hashtable-uri (unordered_map) pentru gestionarea clientilor si a topic-urilor
          la care se aboneaza, accesul la elemente realizandu-se relativ in timp constant

          - client_sockfds -> retine pentru fiecare client (<id>) socket-ul asociat
                           -> cand un client se deconecteaza, intrarea corespunzatoare devine -1

          - client_ids -> pentru fiecare socket de clienti TCP, retine id-ul clientului asociat

          - saved_topics -> retine, pentru fiecare topic, subscriberii abonati la topic avand SF activat 
                                                                                            - SF = 1

          - saved_ids -> retine, pentru fiecare client <id>, topic-urile la care acesta are SF activat
 
          - client_topics -> retine toate topic-urile la care este abonat un client <id>

        > saved_msgs -> stocheaza mesajele pentru retransmiterea lor clientilor corespunzatori cand se
                          reconecteaza

        * se configureaza adresele UDP si TCP si socketii sock_udp, sock_tcp 
          pentru comunicarea cu clientii

        * se pasivizeaza socket-ul de TCP - sock_tcp

        * folosind select(), se identifica fiecare socket pe care se primesc date

            - daca se primeste o comanda de la stdin, se verifica daca aceasta este <exit>,
              caz in care se inchide conexiunea

            - daca se primesc date pe sock_tcp --> o cerere de conexiune de la un client

                  -> se asteapta id-ul clientului

                           - se verifica daca este un client nou (nu a mai fost conectat pana acum)

                           - daca este un client care se reconecteaza, atunci se trimit mesajele
                             corespunzatoare cu SF activat

                           - daca este un client cu acelasi id cu alt client conectat,
                             atunci clientul se inchide

            - daca se primesc date pe sock_udp 

                  -> se parseaza mesajul primit de la clientul UDP - (udp_msg) u_msg
                     
                  -> se preiau informatiile necesare pentru formarea
                     mesajului de trimis subscriberilor TCP - (tcp_msg) t_msg

                  -> se salveaza - daca este cazul - t_msg in saved_msgs pentru
                     logica de store-and-forward

            - daca se primesc date pe un alt socket decat cele de mai sus

                  -> se primeste o comanda de la unul din subscriberi

                       - daca se deconecteaza, atunci se sterge socket-ul clientului
                         din multimea curenta de socketi folositi - read_fds

                             -- se actualizeaza saved_topics pentru salvarea mesajelor
                                la care s-a abonat clientul cu SF activat

                       - daca este o comanda de (un)subscribe, atunci se actualizeaza
                         client_topics si saved_ids (in functie de SF)


